# IDA MCP queries with `curl`

This note records the lightweight way used to query the local IDA MCP server for
the Minecraft 0.15.10.0 executable. It sends one JSON-RPC request at a time and
does not keep an SSE stream open while inspecting results.

## Connection

IDA is listening on `127.0.0.1:13337`.

The server exposes an SSE handshake at `/sse`; `curl.exe -i
http://127.0.0.1:13337/sse` returns an endpoint event with a session URL. For
short MCP queries, the working endpoint in this setup is the direct JSON-RPC
HTTP endpoint `/mcp`:

```powershell
$body = '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}'
curl.exe -sS --max-time 5 -X POST 'http://127.0.0.1:13337/mcp' `
  -H 'Content-Type: application/json' --data-raw $body
```

Use **`curl.exe`**, since PowerShell's `curl` alias may resolve to a different
command. Increment `id` for each request. MCP tool calls use this envelope:

```powershell
$body = '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"decompile","arguments":{"addr":"0x14032A000"}}}'
curl.exe -sS --max-time 8 -X POST 'http://127.0.0.1:13337/mcp' `
  -H 'Content-Type: application/json' --data-raw $body
```

For queries assembled as PowerShell objects, serialize compactly so the JSON
body remains valid:

```powershell
$request = @{
  jsonrpc = '2.0'; id = 3; method = 'tools/call'
  params = @{ name = 'get_bytes'; arguments = @{ regions = @{ addr = '0x140E67628'; size = 96 } } }
}
$body = $request | ConvertTo-Json -Depth 8 -Compress
curl.exe -sS --max-time 5 -X POST 'http://127.0.0.1:13337/mcp' `
  -H 'Content-Type: application/json' --data-raw $body
```

## IDA MCP tools observed

`tools/list` returned the following analysis/editing tool families in this IDA
MCP server. Use only the fields accepted by the live server's `inputSchema`;
these are the query shapes used for this investigation.

| Tool | Query shape | Purpose |
|---|---|---|
| `list_funcs`, `lookup_funcs` | `queries` | Find or resolve function names/addresses. `queries` accepts strings, objects or arrays in this server. |
| `list_globals` | `queries` | Find symbols such as RTTI and vtables. |
| `get_bytes` | `regions` (each has `addr`, `size`) | Read bounded byte ranges, including vtable entries. |
| `get_global_value`, `get_int`, `read_struct` | `queries` | Read global values, integers or IDA structure data. |
| `xrefs_to` | `addrs`, optional `limit` | Find references to symbols/addresses. |
| `xrefs_to_field` | `queries` | Find references to structure fields. |
| `find_regex` | `pattern`, optional `limit`, `offset` | Search strings in the database. |
| `find_bytes` | `patterns`, optional `limit`, `offset` | Search byte patterns. |
| `find` | `targets`, `type`, optional `limit`, `offset` | Search selected target types. |
| `disasm` | `addr`, optional `max_instructions`, `offset`, `include_total` | Inspect a function's ABI and instructions. |
| `decompile` | `addr` | Request Hex-Rays pseudocode. |
| `callees` | `addrs`, optional `limit` | List functions called by selected addresses. |
| `callgraph` | `roots` plus depth/node/edge limits | Explore a focused caller/callee graph. |
| `basic_blocks` | `addrs`, optional `max_blocks`, `offset` | Inspect control-flow blocks. |
| `imports` | optional `count`, `offset` | Inspect imported APIs. |
| `search_structs` | `filter` | Search IDA structure definitions. |
| `get_string` | `addrs` | Read strings at selected addresses. |
| `export_funcs` | `addrs`, `format` | Export selected function metadata. |
| `int_convert` | `inputs` | Convert integer/address representations. |
| `patch`, `patch_asm`, `put_int` | `patches`, `items` | Modify database bytes/values; not used here. |
| `set_comments`, `rename`, `set_type`, `declare_type`, `declare_stack`, `stack_frame`, `define_func`, `define_code`, `undefine`, `delete_stack` | `items`, `batch`, `edits`, `decls`, or `addrs` depending on tool | Edit IDA database metadata; not used here. |
| `debug_mode` | `enable` | Toggle IDA debugging mode. |
| `hexrays_diag` | no arguments | Query Hex-Rays diagnostics. |

If an argument shape is uncertain, request `tools/list` and inspect the selected
tool's `inputSchema` before calling it. Keep searches narrow: query a class
symbol, then its vtable, then only the relevant function(s), instead of dumping
all strings or functions into the response.

## Slot Easing finding (0.15.10.0)

The executable database showed:

- `VMinecraftUIRenderContext` vtable: `0x140E67628`.
- Vtable slot 10 (`vtable + 10 * 8`): `0x14032A000`, RVA `0x32A000`.
- The slot-10 method queues an image-draw command. Its arguments include a
  texture handle, screen position, image size, UV origin and UV size.
- The queued renderer (`sub_14032B4A0`) reads texture dimensions from the
  texture resource and normalizes the queued UV values using those dimensions.
- `hud_screen.json` binds `hotbar_slot_selected_image` to `gui/gui.png`, with
  logical atlas UV origin `(0,22)` and UV size `(24,22)` in a 256x256 atlas.

The previous `HotBarRenderer::render` hook did not produce usable bounds in
runtime logs. Slot Easing now hooks the draw-image function and identifies the
selected frame by its logical UV rectangle, with a normalized-UV fallback for
scaled atlases, then eases the image's X position. This removes the dependency
on `#collection_index` in the renderer callback and does not patch the game's
code. When enabled, it logs the first eight draw-image calls and logs a
`selected image matched` line when the target UV region is found.

## Custom Chat mouse/input findings (0.15.10.0)

Read-only IDA queries for the `VChatScreen` vtable (`0x140E4BB48`) showed:

- Slot 6 points to `sub_1400DCE70`, which sets
  `*(*(screen+0x28)+0x2B0)+0x17F` to `1` when the native chat screen activates.
- Slot 7 points to `sub_1400DCE10`, which calls `sub_1400DDF30` and clears that
  same byte on exit. These callbacks do not directly set the OS cursor or alter
  raw mouse movement/wheel state.
- The SDK's `Client::chatOpen()` identifies the native chat by checking the top
  entry of the screen stack at client offsets `+0x148/+0x150` against the
  `VChatScreen` vtable. Custom Chat does not need to patch or replace this
  screen to keep T from opening it.

The mouse lock was in Azyre's own overlay capture path: `CustomChat::openPanel`
set `menu::uiCapture(true)`, making `menu::uiOwnsInput()` true. `WheelHook` then
zeroed raw mouse buttons and relative movement while the overlay remained open;
the chat drew a software cursor while Bedrock kept ownership of the real cursor.
The new Custom Chat input path captures keyboard messages/raw keyboard only,
keeps `uiCapture` off, and removes the overlay cursor. That leaves raw mouse,
native cursor visibility, and wheel events under the game's normal UI lifecycle.
