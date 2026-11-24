#include "SpotifyService.hpp"

#include "Framework/Log.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace mc::spotify {

namespace {

constexpr int kDefaultPollMs = 1000;
constexpr uint32_t kMaxArtworkBytes = 8 * 1024 * 1024;

std::mutex g_mu;
Track g_track;
std::string g_artKey;
std::vector<unsigned char> g_artBytes;
bool g_artPending = false;
std::string g_lastError;
Status g_status = Status::NoSession;

std::thread g_pollThread;
std::atomic<bool> g_pollStop{true};
std::atomic<int> g_pollMs{kDefaultPollMs};
std::mutex g_pollMu;

std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

std::vector<unsigned char> readThumbnail(
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionMediaProperties const& media)
{
    using namespace winrt;
    using namespace Windows::Storage::Streams;

    std::vector<unsigned char> bytes;
    auto thumbnail = media.Thumbnail();
    if (!thumbnail)
        return bytes;

    auto stream = thumbnail.OpenReadAsync().get();
    uint64_t size = stream.Size();
    if (size == 0 || size > kMaxArtworkBytes)
        return bytes;

    DataReader reader(stream.GetInputStreamAt(0));
    reader.LoadAsync(static_cast<uint32_t>(size)).get();
    bytes.resize(static_cast<size_t>(size));
    reader.ReadBytes(array_view<uint8_t>(bytes));
    return bytes;
}

struct Snapshot
{
    Track track;
    std::string artKey;
    std::vector<unsigned char> artwork;
    std::string error;
};

Snapshot readSpotifySession(std::string const& previousArtKey)
{
    using namespace winrt;
    using namespace Windows::Media::Control;

    Snapshot result;
    auto manager = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
    GlobalSystemMediaTransportControlsSession selected{nullptr};

    // GetCurrentSession can refer to a browser or another media app. Search
    // all registered sessions so the widget stays specific to Spotify.
    auto sessions = manager.GetSessions();
    for (uint32_t i = 0; i < sessions.Size(); ++i)
    {
        auto session = sessions.GetAt(i);
        if (lower(to_string(session.SourceAppUserModelId())).find("spotify") == std::string::npos)
            continue;

        auto info = session.GetPlaybackInfo();
        if (!selected || info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
        {
            selected = session;
            if (info.PlaybackStatus() == GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing)
                break;
        }
    }

    if (!selected)
        return result;

    auto media = selected.TryGetMediaPropertiesAsync().get();
    if (!media)
        return result;

    auto timeline = selected.GetTimelineProperties();
    auto playback = selected.GetPlaybackInfo();
    const std::string title = to_string(media.Title());
    const std::string artist = to_string(media.Artist());
    const std::string album = to_string(media.AlbumTitle());
    const std::string appId = to_string(selected.SourceAppUserModelId());

    result.track.valid = !title.empty() || !artist.empty() || !album.empty();
    result.track.playing = playback.PlaybackStatus() ==
                           GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
    result.track.title = title;
    result.track.artists = artist;
    result.track.album = album;
    result.track.progressMs = timeline.Position().count() / 10000;
    result.track.durationMs = timeline.EndTime().count() / 10000;
    result.track.progressAtTick = GetTickCount64();

    result.artKey = appId + "|" + title + "|" + artist + "|" + album;
    result.track.artworkUrl = result.artKey;
    if (result.track.valid && result.artKey != previousArtKey)
    {
        try
        {
            result.artwork = readThumbnail(media);
        }
        catch (...)
        {
            // Metadata/progress remain useful when the session has no thumbnail.
        }
    }
    return result;
}

void pollOnce()
{
    try
    {
        std::string previousArtKey;
        {
            std::lock_guard<std::mutex> lock(g_mu);
            previousArtKey = g_artKey;
        }

        Snapshot next = readSpotifySession(previousArtKey);

        std::lock_guard<std::mutex> lock(g_mu);
        g_track = std::move(next.track);
        g_status = g_track.valid ? Status::Connected : Status::NoSession;
        g_lastError.clear();
        if (g_track.valid && next.artKey != g_artKey)
        {
            g_artKey = next.artKey;
            g_artBytes = std::move(next.artwork);
            g_artPending = !g_artBytes.empty();
        }
    }
    catch (winrt::hresult_error const& e)
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_track = {};
        g_status = Status::NoSession;
        if (e.code().value == E_ACCESSDENIED)
        {
            g_lastError = "GSMTC is blocked: Minecraft's package is missing the globalMediaControl capability.";
            g_pollStop.store(true, std::memory_order_relaxed);
        }
        else
        {
            g_lastError = winrt::to_string(e.message());
        }
        MC_LOG_ERROR("[Spotify] GSMTC access failed: %s", g_lastError.c_str());
    }
    catch (...)
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_track = {};
        g_status = Status::NoSession;
        g_lastError = "Unknown error reading Windows media session";
    }
}

void pollLoop()
{
    try
    {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        while (!g_pollStop.load(std::memory_order_relaxed))
        {
            pollOnce();
            const int waitMs = std::clamp(g_pollMs.load(std::memory_order_relaxed), 250, 10000);
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs);
            while (!g_pollStop.load(std::memory_order_relaxed) && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        winrt::uninit_apartment();
    }
    catch (winrt::hresult_error const& e)
    {
        std::lock_guard<std::mutex> lock(g_mu);
        g_status = Status::NoSession;
        g_lastError = winrt::to_string(e.message());
        MC_LOG_ERROR("[Spotify] GSMTC worker failed: %s", g_lastError.c_str());
    }
}

} // namespace

Status status()
{
    std::lock_guard<std::mutex> lock(g_mu);
    return g_status;
}

std::string lastError()
{
    std::lock_guard<std::mutex> lock(g_mu);
    return g_lastError;
}

void startPolling()
{
    std::lock_guard<std::mutex> lock(g_pollMu);
    if (g_pollThread.joinable())
        return;
    g_pollStop.store(false, std::memory_order_relaxed);
    g_pollThread = std::thread(pollLoop);
}

void stopPolling()
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(g_pollMu);
        if (!g_pollThread.joinable())
            return;
        g_pollStop.store(true, std::memory_order_relaxed);
        worker = std::move(g_pollThread);
    }
    worker.join();
    std::lock_guard<std::mutex> lock(g_mu);
    g_track = {};
    g_status = Status::NoSession;
}

void shutdown()
{
    stopPolling();
}

int pollIntervalMs()
{
    return g_pollMs.load(std::memory_order_relaxed);
}

void setPollIntervalMs(int ms)
{
    g_pollMs.store(std::clamp(ms, 250, 10000), std::memory_order_relaxed);
}

Track track()
{
    std::lock_guard<std::mutex> lock(g_mu);
    return g_track;
}

bool takeArtwork(std::string& key, std::vector<unsigned char>& bytes)
{
    std::lock_guard<std::mutex> lock(g_mu);
    if (!g_artPending)
        return false;
    key = g_artKey;
    bytes = std::move(g_artBytes);
    g_artBytes.clear();
    g_artPending = false;
    return true;
}

} // namespace mc::spotify
