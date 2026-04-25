/* apilib.h — Compatibility header for legacy app sources.
 *
 * 기존 harib27f/<app>/<app>.c 들이 #include "apilib.h" 형태로 의존하므로
 * 동일 경로로 노출하기 위해 둔다. 실제 정의는 bxos.h 에 있다.
 */
#ifndef APILIB_H
#define APILIB_H
#include "bxos.h"
#endif
