#include "weread_browser.h"

WereadBrowser::WereadBrowser(const QUrl &url, FbRefreshHelper *fbRef)
    : QMainWindow(), m_fbRef(fbRef) {
  initProfileAndView(url);
  initLoadSignals();
  initNavigationSignals();
  initStartupLoad(url);
  initIdleCleanup();
  initSessionAutoSave();
  initMenuOverlay();
  initStateResponder();
}
