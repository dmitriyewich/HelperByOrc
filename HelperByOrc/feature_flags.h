#pragma once

#define HELPERBYORC_ENABLE_ARIZONA_INTEGRATION 0
#define HELPERBYORC_ENABLE_CHAT_ASI_INTEGRATION HELPERBYORC_ENABLE_ARIZONA_INTEGRATION

namespace feature_flags {

// Temporary diagnostic build: keep Arizona-specific integrations disabled.
inline constexpr bool kEnableArizonaIntegration = HELPERBYORC_ENABLE_ARIZONA_INTEGRATION != 0;
inline constexpr bool kEnableChatAsiIntegration = HELPERBYORC_ENABLE_CHAT_ASI_INTEGRATION != 0;

} // namespace feature_flags
