#ifndef HORSE_UI_UI_HPP
#define HORSE_UI_UI_HPP

namespace UI {

bool init();
void shutdown();
bool beginFrame();
void showOverlay();
bool initialized();
bool wantsMouse();
bool wantsKeyboard();

} // namespace UI

#endif
