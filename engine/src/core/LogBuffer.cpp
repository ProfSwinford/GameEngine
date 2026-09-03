// =============================================================================
//  LogBuffer.cpp - a skeleton. Every function is here with the right signature
//  and an empty body. LogBuffer.h is the specification; read it first.
// =============================================================================

#include <engine/core/LogBuffer.h>

namespace eng {

// Sets how many recent messages are kept. Older ones fall off the end, so a
// program left running overnight does not fill memory with its own log.
void LogBuffer::SetCapacity(std::size_t /*capacity*/) {
}

// How many messages the buffer is currently willing to hold.
std::size_t LogBuffer::Capacity() {
    return 0;
}

// Adds one message. Called by Log::Write, and by nothing else.
void LogBuffer::Append(const LogRecord& /*record*/) {
}

// Copies the whole buffer out for the Console window to draw. A copy, because
// the buffer can change while the window is being drawn.
void LogBuffer::Snapshot(std::vector<LogRecord>& /*out*/) {
}

// Lists every channel name seen so far, which is what fills the Console's
// channel filter without anyone having to declare the list up front.
void LogBuffer::Channels(std::vector<std::string>& /*out*/) {
}

// How many messages are being held right now.
std::size_t LogBuffer::Count() {
    return 0;
}

// How many messages have ever been written, including ones already dropped.
unsigned long long LogBuffer::TotalWritten() {
    return 0;
}

// Empties the buffer - the Console's Clear button.
void LogBuffer::Clear() {
}

} // namespace eng
