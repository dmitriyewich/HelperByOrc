#pragma once

#define HELPERBYORC_ENABLE_ARIZONA_INTEGRATION 1
#define HELPERBYORC_ENABLE_CHAT_ASI_INTEGRATION HELPERBYORC_ENABLE_ARIZONA_INTEGRATION

namespace feature_flags {

// Arizona-specific integrations are active in the normal build.
inline constexpr bool kEnableArizonaIntegration = HELPERBYORC_ENABLE_ARIZONA_INTEGRATION != 0;
inline constexpr bool kEnableChatAsiIntegration = HELPERBYORC_ENABLE_CHAT_ASI_INTEGRATION != 0;

} // namespace feature_flags
