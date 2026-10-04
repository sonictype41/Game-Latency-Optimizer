# GLO OSS roadmap

The Windows app and Linux relay are service-agnostic OSS binaries. Service-only functionality remains outside the client and relay.

Near-term OSS work should focus on soak testing one-time redemption, relay capacity behavior, route handover, mixed-session fairness and real-game measurements. New games should be added as logical profiles without introducing filesystem paths into portable session configs.

Future service changes should continue to use the documented client/session contract without requiring service-only state inside the OSS client or relay.
