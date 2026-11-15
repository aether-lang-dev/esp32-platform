# Portable controller core

Reserved boundary for shared Aether device models, command preparation and
public enrollment data. Extract working code here when the native controller
uses it; do not duplicate the web/Python transport speculatively.

Private keys and PSKs remain behind platform-owned opaque handles. This layer
must not depend on UIKit, WebKit, ESP-IDF or a cloud account.
