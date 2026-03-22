#pragma once
// ── i18n compatibility shim ───────────────────────────────────────────────────
// When building WITH KDE Frameworks (HAVE_KF=1) we use KLocalizedString's i18n()
// macro family directly.  When building WITHOUT KDE Frameworks (HAVE_KF=0) we
// route all i18n() calls through QCoreApplication::translate() so that a loaded
// QTranslator (from the embedded .qm resource) is consulted at runtime.
//
// The translation context used is "fritzhome", matching the <context> element
// in translations/fritzhome_de.ts.

#if HAVE_KF
#  include <KLocalizedString>
#else
#  include <QString>
#  include <QCoreApplication>
// i18n("text")          → QCoreApplication::translate("fritzhome", "text")
// i18n("text %1", arg)  → translate(…).arg(arg)   etc.
namespace FritzI18n {
    inline QString tr0(const char *s)
    {
        return QCoreApplication::translate("fritzhome", s);
    }

    template<typename A1>
    inline QString tr1(const char *s, A1 a1)
    {
        return QCoreApplication::translate("fritzhome", s).arg(a1);
    }

    template<typename A1, typename A2>
    inline QString tr2(const char *s, A1 a1, A2 a2)
    {
        return QCoreApplication::translate("fritzhome", s).arg(a1).arg(a2);
    }

    template<typename A1, typename A2, typename A3>
    inline QString tr3(const char *s, A1 a1, A2 a2, A3 a3)
    {
        return QCoreApplication::translate("fritzhome", s).arg(a1).arg(a2).arg(a3);
    }
}

// Count the number of arguments passed (up to 3 extra args supported here).
#define _I18N_NARG(...)  _I18N_NARG_IMPL(__VA_ARGS__, 3, 2, 1, 0)
#define _I18N_NARG_IMPL(_1, _2, _3, _4, N, ...) N

#define _I18N_0(s)                 FritzI18n::tr0(s)
#define _I18N_1(s, a1)             FritzI18n::tr1(s, a1)
#define _I18N_2(s, a1, a2)         FritzI18n::tr2(s, a1, a2)
#define _I18N_3(s, a1, a2, a3)     FritzI18n::tr3(s, a1, a2, a3)

#define _I18N_DISPATCH(N, ...) _I18N_ ## N(__VA_ARGS__)
#define _I18N_DISPATCH2(N, ...) _I18N_DISPATCH(N, __VA_ARGS__)

#define i18n(...) _I18N_DISPATCH2(_I18N_NARG(__VA_ARGS__), __VA_ARGS__)

// ki18n / i18nc / i18np are not used in this codebase, but provide stubs just
// in case:
#define i18nc(ctx, s, ...) i18n(s, ##__VA_ARGS__)
#define i18np(s, p, n, ...) ((n)==1 ? i18n(s, n, ##__VA_ARGS__) : i18n(p, n, ##__VA_ARGS__))
#endif
