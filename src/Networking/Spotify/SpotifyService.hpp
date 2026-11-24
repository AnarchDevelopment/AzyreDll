#pragma once

#include <string>
#include <vector>

namespace mc::spotify {

struct Track
{
    bool valid = false;
    bool playing = false;
    std::string title;
    std::string artists;
    std::string album;
    std::string artworkUrl; // Internal stable key; no network URL is used.
    long long progressMs = 0;
    long long durationMs = 0;
    unsigned long long progressAtTick = 0;
};

enum class Status
{
    NoSession,
    Connected,
};

void startPolling();
void stopPolling();
void shutdown();

Status status();
std::string lastError();
int pollIntervalMs();
void setPollIntervalMs(int ms);

Track track();
bool takeArtwork(std::string& key, std::vector<unsigned char>& bytes);

} // namespace mc::spotify
