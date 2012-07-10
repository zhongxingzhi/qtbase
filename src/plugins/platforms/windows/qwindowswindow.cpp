/****************************************************************************
**
** Copyright (C) 2012 Nokia Corporation and/or its subsidiary(-ies).
** Contact: http://www.qt-project.org/
**
** This file is part of the plugins of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** GNU Lesser General Public License Usage
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this
** file. Please review the following information to ensure the GNU Lesser
** General Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU General
** Public License version 3.0 as published by the Free Software Foundation
** and appearing in the file LICENSE.GPL included in the packaging of this
** file. Please review the following information to ensure the GNU General
** Public License version 3.0 requirements will be met:
** http://www.gnu.org/copyleft/gpl.html.
**
** Other Usage
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**
**
**
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "qwindowswindow.h"
#include "qwindowsnativeimage.h"
#include "qwindowscontext.h"
#include "qwindowsdrag.h"
#include "qwindowsscreen.h"
#include "qwindowscursor.h"

#ifdef QT_OPENGL_ES_2
#  include "qwindowseglcontext.h"
#endif

#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtGui/QRegion>
#include <private/qwindow_p.h>
#include <QtGui/qwindowsysteminterface.h>

#include <QtCore/QDebug>

QT_BEGIN_NAMESPACE

static QByteArray debugWinStyle(DWORD style)
{
    QByteArray rc = "0x";
    rc += QByteArray::number(qulonglong(style), 16);
    if (style & WS_POPUP)
        rc += " WS_POPUP";
    if (style & WS_CHILD)
        rc += " WS_CHILD";
    if (style & WS_OVERLAPPED)
        rc += " WS_OVERLAPPED";
    if (style & WS_CLIPSIBLINGS)
        rc += " WS_CLIPSIBLINGS";
    if (style & WS_CLIPCHILDREN)
        rc += " WS_CLIPCHILDREN";
    if (style & WS_THICKFRAME)
        rc += " WS_THICKFRAME";
    if (style & WS_DLGFRAME)
        rc += " WS_DLGFRAME";
    if (style & WS_SYSMENU)
        rc += " WS_SYSMENU";
    if (style & WS_MINIMIZEBOX)
        rc += " WS_MINIMIZEBOX";
    if (style & WS_MAXIMIZEBOX)
        rc += " WS_MAXIMIZEBOX";
    return rc;
}

static QByteArray debugWinExStyle(DWORD exStyle)
{
    QByteArray rc = "0x";
    rc += QByteArray::number(qulonglong(exStyle), 16);
    if (exStyle & WS_EX_TOOLWINDOW)
        rc += " WS_EX_TOOLWINDOW";
    if (exStyle & WS_EX_CONTEXTHELP)
        rc += " WS_EX_CONTEXTHELP";
    if (exStyle & WS_EX_LAYERED)
        rc += " WS_EX_LAYERED";
    return rc;
}

static QByteArray debugWindowStates(Qt::WindowStates s)
{

    QByteArray rc = "0x";
    rc += QByteArray::number(int(s), 16);
    if (s & Qt::WindowMinimized)
        rc += " WindowMinimized";
    if (s & Qt::WindowMaximized)
        rc += " WindowMaximized";
    if (s & Qt::WindowFullScreen)
        rc += " WindowFullScreen";
    if (s & Qt::WindowActive)
        rc += " WindowActive";
    return rc;
}

#ifndef Q_OS_WINCE // maybe available on some SDKs revisit WM_GETMINMAXINFO
QDebug operator<<(QDebug d, const MINMAXINFO &i)
{
    d.nospace() << "MINMAXINFO maxSize=" << i.ptMaxSize.x << ','
                << i.ptMaxSize.y << " maxpos=" << i.ptMaxPosition.x
                 << ',' << i.ptMaxPosition.y << " mintrack="
                 << i.ptMinTrackSize.x << ',' << i.ptMinTrackSize.y
                 << " maxtrack=" << i.ptMaxTrackSize.x << ','
                 << i.ptMaxTrackSize.y;
    return d;
}
#endif // !Q_OS_WINCE

static inline QSize qSizeOfRect(const RECT &rect)
{
    return QSize(rect.right -rect.left, rect.bottom - rect.top);
}

static inline QRect qrectFromRECT(const RECT &rect)
{
    return QRect(QPoint(rect.left, rect.top), qSizeOfRect(rect));
}

static inline RECT RECTfromQRect(const QRect &rect)
{
    const int x = rect.left();
    const int y = rect.top();
    RECT result = { x, y, x + rect.width(), y + rect.height() };
    return result;
}

QDebug operator<<(QDebug d, const RECT &r)
{
    d.nospace() << "RECT: left/top=" << r.left << ',' << r.top
                << " right/bottom=" << r.right << ',' << r.bottom;
    return d;
}

#ifndef Q_OS_WINCE // maybe available on some SDKs revisit WM_NCCALCSIZE
QDebug operator<<(QDebug d, const NCCALCSIZE_PARAMS &p)
{
    qDebug().nospace() << "NCCALCSIZE_PARAMS "
        << qrectFromRECT(p.rgrc[0])
        << ' ' << qrectFromRECT(p.rgrc[1]) << ' '
        << qrectFromRECT(p.rgrc[2]);
    return d;
}
#endif // !Q_OS_WINCE

// Return the frame geometry relative to the parent
// if there is one.
static inline QRect frameGeometry(HWND hwnd, bool topLevel)
{
    RECT rect = { 0, 0, 0, 0 };
    GetWindowRect(hwnd, &rect); // Screen coordinates.
    const HWND parent = GetParent(hwnd);
    if (parent && !topLevel) {
        const int width = rect.right - rect.left;
        const int height = rect.bottom - rect.top;
        POINT leftTop = { rect.left, rect.top };
        ScreenToClient(parent, &leftTop);
        rect.left = leftTop.x;
        rect.top = leftTop.y;
        rect.right = leftTop.x + width;
        rect.bottom = leftTop.y + height;
    }
    return qrectFromRECT(rect);
}

static inline QSize clientSize(HWND hwnd)
{
    RECT rect = { 0, 0, 0, 0 };
    GetClientRect(hwnd, &rect); // Always returns point 0,0, thus unusable for geometry.
    return qSizeOfRect(rect);
}

// from qwidget_win.cpp/maximum layout size check removed.
static bool shouldShowMaximizeButton(Qt::WindowFlags flags)
{
    if (flags & Qt::MSWindowsFixedSizeDialogHint)
        return false;
    // if the user explicitly asked for the maximize button, we try to add
    // it even if the window has fixed size.
    if (flags & Qt::CustomizeWindowHint &&
        flags & Qt::WindowMaximizeButtonHint)
        return true;
    return flags & Qt::WindowMaximizeButtonHint;
}

static void setWindowOpacity(HWND hwnd, Qt::WindowFlags flags, qreal level)
{
#ifdef Q_OS_WINCE // maybe needs revisit WS_EX_LAYERED
    Q_UNUSED(hwnd);
    Q_UNUSED(flags);
    Q_UNUSED(level);
#else
    const long wl = GetWindowLong(hwnd, GWL_EXSTYLE);
    const bool isOpaque = level == 1.0 && !(flags & Qt::WindowTransparentForInput);

    if (isOpaque) {
        if (wl & WS_EX_LAYERED)
            SetWindowLong(hwnd, GWL_EXSTYLE, wl & ~WS_EX_LAYERED);
    } else {
        if ((wl & WS_EX_LAYERED) == 0)
            SetWindowLong(hwnd, GWL_EXSTYLE, wl | WS_EX_LAYERED);
        if (flags & Qt::FramelessWindowHint) {
            BLENDFUNCTION blend = {AC_SRC_OVER, 0, (int)(255.0 * level), AC_SRC_ALPHA};
            QWindowsContext::user32dll.updateLayeredWindow(hwnd, NULL, NULL, NULL, NULL, NULL, 0, &blend, ULW_ALPHA);
        } else {
            QWindowsContext::user32dll.setLayeredWindowAttributes(hwnd, 0, (int)(level * 255), LWA_ALPHA);
        }
    }
#endif // !Q_OS_WINCE
}

/*!
    \class WindowCreationData
    \brief Window creation code.

    This struct gathers all information required to create a window.
    Window creation is split in 3 steps:

    \list
    \li fromWindow() Gather all required information
    \li create() Create the system handle.
    \li initialize() Post creation initialization steps.
    \endlist

    The reason for this split is to also enable changing the QWindowFlags
    by calling:

    \list
    \li fromWindow() Gather information and determine new system styles
    \li applyWindowFlags() to apply the new window system styles.
    \li initialize() Post creation initialization steps.
    \endlist

    Contains the window creation code formerly in qwidget_win.cpp.

    \sa QWindowCreationContext
    \internal
    \ingroup qt-lighthouse-win
*/

struct WindowCreationData
{
    typedef QWindowsWindow::WindowData WindowData;
    enum Flags { ForceChild = 0x1 };

    WindowCreationData() : parentHandle(0), type(Qt::Widget), style(0), exStyle(0),
        topLevel(false), popup(false), dialog(false), desktop(false),
        tool(false), embedded(false) {}

    void fromWindow(const QWindow *w, const Qt::WindowFlags flags, unsigned creationFlags = 0);
    inline WindowData create(const QWindow *w, const QRect &geometry, QString title) const;
    inline void applyWindowFlags(HWND hwnd) const;
    void initialize(HWND h, bool frameChange, qreal opacityLevel) const;

    Qt::WindowFlags flags;
    HWND parentHandle;
    Qt::WindowType type;
    unsigned style;
    unsigned exStyle;
    bool isGL;
    bool topLevel;
    bool popup;
    bool dialog;
    bool desktop;
    bool tool;
    bool embedded;
};

QDebug operator<<(QDebug debug, const WindowCreationData &d)
{
    debug.nospace() << QWindowsWindow::debugWindowFlags(d.flags)
        << " GL=" << d.isGL << " topLevel=" << d.topLevel << " popup="
        << d.popup << " dialog=" << d.dialog << " desktop=" << d.desktop
        << " embedded=" << d.embedded
        << " tool=" << d.tool << " style=" << debugWinStyle(d.style)
        << " exStyle=" << debugWinExStyle(d.exStyle)
        << " parent=" << d.parentHandle;
    return debug;
}

void WindowCreationData::fromWindow(const QWindow *w, const Qt::WindowFlags flagsIn,
                                    unsigned creationFlags)
{
    isGL = w->surfaceType() == QWindow::OpenGLSurface;
    flags = flagsIn;

    // Sometimes QWindow doesn't have a QWindow parent but does have a native parent window,
    // e.g. in case of embedded ActiveQt servers. They should not be considered a top-level
    // windows in such cases.
    QVariant prop = w->property("_q_embedded_native_parent_handle");
    if (prop.isValid()) {
        embedded = true;
        parentHandle = (HWND)prop.value<WId>();
    }

    topLevel = ((creationFlags & ForceChild) || embedded) ? false : w->isTopLevel();

    if (topLevel && flags == 1) {
        qWarning("Remove me: fixing toplevel window flags");
        flags |= Qt::WindowTitleHint|Qt::WindowSystemMenuHint|Qt::WindowMinimizeButtonHint
                |Qt::WindowMaximizeButtonHint|Qt::WindowCloseButtonHint;
    }

    type = static_cast<Qt::WindowType>(int(flags) & Qt::WindowType_Mask);
    switch (type) {
    case Qt::Dialog:
    case Qt::Sheet:
        dialog = true;
        break;
    case Qt::Drawer:
    case Qt::Tool:
        tool = true;
        break;
    case Qt::Popup:
        popup = true;
        break;
    case Qt::Desktop:
        desktop = true;
        break;
    default:
        break;
    }
    if ((flags & Qt::MSWindowsFixedSizeDialogHint))
        dialog = true;

    // Parent: Use transient parent for top levels.
    if (popup) {
        flags |= Qt::WindowStaysOnTopHint; // a popup stays on top, no parent.
    } else if (!embedded) {
        if (const QWindow *parentWindow = topLevel ? w->transientParent() : w->parent())
            parentHandle = QWindowsWindow::handleOf(parentWindow);
    }

    if (popup || (type == Qt::ToolTip) || (type == Qt::SplashScreen)) {
        style = WS_POPUP;
    } else if (topLevel && !desktop) {
        if (flags & Qt::FramelessWindowHint)
            style = WS_POPUP;                // no border
        else if (flags & Qt::WindowTitleHint)
            style = WS_OVERLAPPED;
        else
            style = 0;
    } else {
        style = WS_CHILD;
    }

    if (!desktop) {
        // if (!testAttribute(Qt::WA_PaintUnclipped))
        // ### Commented out for now as it causes some problems, but
        // this should be correct anyway, so dig some more into this
#ifdef Q_FLATTEN_EXPOSE
        if (isGL)
            style |= WS_CLIPSIBLINGS | WS_CLIPCHILDREN; // see SetPixelFormat
#else
        style |= WS_CLIPSIBLINGS | WS_CLIPCHILDREN ;
#endif
        if (topLevel) {
            if ((type == Qt::Window || dialog || tool)) {
                if (!(flags & Qt::FramelessWindowHint)) {
                    style |= WS_POPUP;
                    if (flags & Qt::MSWindowsFixedSizeDialogHint) {
                        style |= WS_DLGFRAME;
                    } else {
                        style |= WS_THICKFRAME;
                    }
                    if (flags & Qt::WindowTitleHint)
                        style |= WS_CAPTION; // Contains WS_DLGFRAME
                }
                if (flags & Qt::WindowSystemMenuHint)
                    style |= WS_SYSMENU;
                if (flags & Qt::WindowMinimizeButtonHint)
                    style |= WS_MINIMIZEBOX;
                if (shouldShowMaximizeButton(flags))
                    style |= WS_MAXIMIZEBOX;
                if (tool)
                    exStyle |= WS_EX_TOOLWINDOW;
                if (flags & Qt::WindowContextHelpButtonHint)
                    exStyle |= WS_EX_CONTEXTHELP;
            } else {
                 exStyle |= WS_EX_TOOLWINDOW;
            }

#ifndef Q_OS_WINCE
            // make mouse events fall through this window
            // NOTE: WS_EX_TRANSPARENT flag can make mouse inputs fall through a layered window
            if (flagsIn & Qt::WindowTransparentForInput)
                exStyle |= WS_EX_LAYERED | WS_EX_TRANSPARENT;
#endif
        }
    }
}

QWindowsWindow::WindowData
    WindowCreationData::create(const QWindow *w, const QRect &geometry, QString title) const
{
    typedef QSharedPointer<QWindowCreationContext> QWindowCreationContextPtr;

    WindowData result;
    result.flags = flags;

    if (desktop) {                        // desktop widget. No frame, hopefully?
        result.hwnd = GetDesktopWindow();
        result.geometry = frameGeometry(result.hwnd, true);
        if (QWindowsContext::verboseWindows)
            qDebug().nospace() << "Created desktop window " << w << result.hwnd;
        return result;
    }

    const HINSTANCE appinst = (HINSTANCE)GetModuleHandle(0);

    const QString windowClassName = QWindowsContext::instance()->registerWindowClass(w, isGL);

    if (title.isEmpty() && (result.flags & Qt::WindowTitleHint))
        title = topLevel ? qAppName() : w->objectName();

    const wchar_t *titleUtf16 = reinterpret_cast<const wchar_t *>(title.utf16());
    const wchar_t *classNameUtf16 = reinterpret_cast<const wchar_t *>(windowClassName.utf16());

    // Capture events before CreateWindowEx() returns.
    const QWindowCreationContextPtr context(new QWindowCreationContext(w, geometry, style, exStyle));
    QWindowsContext::instance()->setWindowCreationContext(context);

    if (QWindowsContext::verboseWindows)
        qDebug().nospace()
                << "CreateWindowEx: " << w << *this
                << " class=" <<windowClassName << " title=" << title
                << "\nrequested: " << geometry << ": "
                << context->frameWidth << 'x' <<  context->frameHeight
                << '+' << context->frameX << '+' << context->frameY;

    result.hwnd = CreateWindowEx(exStyle, classNameUtf16, titleUtf16,
                                 style,
                                 context->frameX, context->frameY,
                                 context->frameWidth, context->frameHeight,
                                 parentHandle, NULL, appinst, NULL);
    QWindowsContext::instance()->setWindowCreationContext(QWindowCreationContextPtr());
    if (QWindowsContext::verboseWindows)
        qDebug().nospace()
                << "CreateWindowEx: returns " << w << ' ' << result.hwnd << " obtained geometry: "
                << context->obtainedGeometry << context->margins;

    if (!result.hwnd) {
        qErrnoWarning("%s: CreateWindowEx failed", __FUNCTION__);
        return result;
    }

    result.geometry = context->obtainedGeometry;
    result.frame = context->margins;
    result.embedded = embedded;
    return result;
}

void WindowCreationData::applyWindowFlags(HWND hwnd) const
{
    // Keep enabled and visible from the current style.
    const LONG_PTR oldStyle = GetWindowLongPtr(hwnd, GWL_STYLE);
    const LONG_PTR oldExStyle = GetWindowLongPtr(hwnd, GWL_EXSTYLE);

    const LONG_PTR newStyle = style | (oldStyle & (WS_DISABLED|WS_VISIBLE));
    if (oldStyle != newStyle)
        SetWindowLongPtr(hwnd, GWL_STYLE, newStyle);
    const LONG_PTR newExStyle = exStyle;
    if (newExStyle != oldExStyle)
        SetWindowLongPtr(hwnd, GWL_EXSTYLE, newExStyle);
    if (QWindowsContext::verboseWindows)
        qDebug().nospace() << __FUNCTION__ << hwnd << *this
        << "\n    Style from " << debugWinStyle(oldStyle) << "\n    to "
        << debugWinStyle(newStyle) << "\n    ExStyle from "
        << debugWinExStyle(oldExStyle) << " to "
        << debugWinExStyle(newExStyle);
}

void WindowCreationData::initialize(HWND hwnd, bool frameChange, qreal opacityLevel) const
{
    if (desktop || !hwnd)
        return;
    UINT swpFlags = SWP_NOMOVE | SWP_NOSIZE;
    if (frameChange)
        swpFlags |= SWP_FRAMECHANGED;
    if (topLevel) {
        swpFlags |= SWP_NOACTIVATE;
        if ((flags & Qt::WindowStaysOnTopHint) || (type == Qt::ToolTip)) {
            SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, swpFlags);
            if (flags & Qt::WindowStaysOnBottomHint)
                qWarning() << "QWidget: Incompatible window flags: the window can't be on top and on bottom at the same time";
        } else if (flags & Qt::WindowStaysOnBottomHint) {
            SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0, swpFlags);
        }
        if (flags & (Qt::CustomizeWindowHint|Qt::WindowTitleHint)) {
            HMENU systemMenu = GetSystemMenu(hwnd, FALSE);
            if (flags & Qt::WindowCloseButtonHint)
                EnableMenuItem(systemMenu, SC_CLOSE, MF_BYCOMMAND|MF_ENABLED);
            else
                EnableMenuItem(systemMenu, SC_CLOSE, MF_BYCOMMAND|MF_GRAYED);
        }

        setWindowOpacity(hwnd, flags, opacityLevel);
    } else { // child.
        SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, swpFlags);
    }
}

/*!
    \class QWindowsGeometryHint
    \brief Stores geometry constraints and provides utility functions.

    Geometry constraints ready to apply to a MINMAXINFO taking frame
    into account.

    \internal
    \ingroup qt-lighthouse-win
*/

#define QWINDOWSIZE_MAX ((1<<24)-1)

QWindowsGeometryHint::QWindowsGeometryHint(const QWindow *w) :
     minimumSize(w->minimumSize()),
     maximumSize(w->maximumSize())
{
}

bool QWindowsGeometryHint::validSize(const QSize &s) const
{
    const int width = s.width();
    const int height = s.height();
    return width >= minimumSize.width() && width <= maximumSize.width()
           && height >= minimumSize.height() && height <= maximumSize.height();
}

QMargins QWindowsGeometryHint::frame(DWORD style, DWORD exStyle)
{
    RECT rect = {0,0,0,0};
#ifndef Q_OS_WINCE
    style &= ~(WS_OVERLAPPED); // Not permitted, see docs.
#endif
    if (!AdjustWindowRectEx(&rect, style, FALSE, exStyle))
        qErrnoWarning("%s: AdjustWindowRectEx failed", __FUNCTION__);
    const QMargins result(qAbs(rect.left), qAbs(rect.top),
                          qAbs(rect.right), qAbs(rect.bottom));
    if (QWindowsContext::verboseWindows)
        qDebug().nospace() << __FUNCTION__ << " style= 0x"
                 << QString::number(style, 16)
                 << " exStyle=0x" << QString::number(exStyle, 16) << ' ' << rect << ' ' << result;

    return result;
}

#ifndef Q_OS_WINCE
void QWindowsGeometryHint::applyToMinMaxInfo(HWND hwnd, MINMAXINFO *mmi) const
{
    return applyToMinMaxInfo(GetWindowLong(hwnd, GWL_STYLE),
                             GetWindowLong(hwnd, GWL_EXSTYLE), mmi);
}

void QWindowsGeometryHint::applyToMinMaxInfo(DWORD style, DWORD exStyle, MINMAXINFO *mmi) const
{
    if (QWindowsContext::verboseWindows)
        qDebug().nospace() << '>' << __FUNCTION__ << '<' << " min="
                           << minimumSize.width() << ',' << minimumSize.height()
                           << " max=" << maximumSize.width() << ',' << maximumSize.height()
                           << " in " << *mmi;

    const QMargins margins = QWindowsGeometryHint::frame(style, exStyle);
    const int frameWidth = margins.left() + margins.right();
    const int frameHeight = margins.top() + margins.bottom();
    if (minimumSize.width() > 0)
        mmi->ptMinTrackSize.x = minimumSize.width() + frameWidth;
    if (minimumSize.height() > 0)
        mmi->ptMinTrackSize.y = minimumSize.height() + frameHeight;

    const int maximumWidth = qMax(maximumSize.width(), minimumSize.width());
    const int maximumHeight = qMax(maximumSize.height(), minimumSize.height());
    if (maximumWidth < QWINDOWSIZE_MAX)
        mmi->ptMaxTrackSize.x = maximumWidth + frameWidth;
    // windows with title bar have an implicit size limit of 112 pixels
    if (maximumHeight < QWINDOWSIZE_MAX)
        mmi->ptMaxTrackSize.y = qMax(maximumHeight + frameHeight, 112);
    if (QWindowsContext::verboseWindows)
        qDebug().nospace() << '<' << __FUNCTION__
                           << " frame=" << margins << ' ' << frameWidth << ',' << frameHeight
                           << " out " << *mmi;
}
#endif // !Q_OS_WINCE

bool QWindowsGeometryHint::positionIncludesFrame(const QWindow *w)
{
    return qt_window_private(const_cast<QWindow *>(w))->positionPolicy
           == QWindowPrivate::WindowFrameInclusive;
}

/*!
    \class QWindowCreationContext
    \brief Active Context for creating windows.

    There is a phase in window creation (WindowCreationData::create())
    in which events are sent before the system API CreateWindowEx() returns
    the handle. These cannot be handled by the platform window as the association
    of the unknown handle value to the window does not exist yet and as not
    to trigger recursive handle creation, etc.

    In that phase, an instance of  QWindowCreationContext is set on
    QWindowsContext.

    QWindowCreationContext stores the information to answer the initial
    WM_GETMINMAXINFO and obtains the corrected size/position.

    \sa WindowCreationData, QWindowsContext
    \internal
    \ingroup qt-lighthouse-win
*/

QWindowCreationContext::QWindowCreationContext(const QWindow *w,
                                               const QRect &geometry,
                                               DWORD style_, DWORD exStyle_) :
    geometryHint(w), style(style_), exStyle(exStyle_),
    requestedGeometry(geometry), obtainedGeometry(geometry),
    margins(QWindowsGeometryHint::frame(style, exStyle)),
    frameX(CW_USEDEFAULT), frameY(CW_USEDEFAULT),
    frameWidth(CW_USEDEFAULT), frameHeight(CW_USEDEFAULT)
{
    // Geometry of toplevels does not consider window frames.
    // TODO: No concept of WA_wasMoved yet that would indicate a
    // CW_USEDEFAULT unless set. For now, assume that 0,0 means 'default'
    // for toplevels.
    if (geometry.isValid()) {
        frameX = geometry.x();
        frameY = geometry.y();
        frameWidth = margins.left() + geometry.width() + margins.right();
        frameHeight = margins.top() + geometry.height() + margins.bottom();
        const bool isDefaultPosition = !frameX && !frameY && w->isTopLevel();
        if (!QWindowsGeometryHint::positionIncludesFrame(w) && !isDefaultPosition) {
            frameX -= margins.left();
            frameY -= margins.top();
        }
    }
    if (QWindowsContext::verboseWindows)
        qDebug().nospace()
                << __FUNCTION__ << ' ' << w << geometry
                << " pos incl. frame" << QWindowsGeometryHint::positionIncludesFrame(w)
                << " frame: " << frameWidth << 'x' << frameHeight << '+'
                << frameX << '+' << frameY
                << " min" << geometryHint.minimumSize
                << " max" << geometryHint.maximumSize;
}

/*!
    \class QWindowsWindow
    \brief Raster or OpenGL Window.

    \list
    \li Raster type: handleWmPaint() is implemented to
       to bitblt the image. The DC can be accessed
       via getDC/Relase DC, which has a special handling
       when within a paint event (in that case, the DC obtained
       from BeginPaint() is returned).

    \li Open GL: The first time QWindowsGLContext accesses
       the handle, it sets up the pixelformat on the DC
       which in turn sets it on the window (see flag
       PixelFormatInitialized).
       handleWmPaint() is empty (although required).
    \endlist

    \internal
    \ingroup qt-lighthouse-win
*/

QWindowsWindow::QWindowsWindow(QWindow *aWindow, const WindowData &data) :
    QPlatformWindow(aWindow),
    m_data(data),
    m_flags(0),
    m_hdc(0),
    m_windowState(aWindow->windowState()),
    m_opacity(1.0),
    m_mouseGrab(false),
    m_cursor(QWindowsScreen::screenOf(aWindow)->windowsCursor()->standardWindowCursor()),
    m_dropTarget(0),
    m_savedStyle(0),
    m_format(aWindow->format())
#ifdef QT_OPENGL_ES_2
   , m_eglSurface(0)
#endif
{
    if (aWindow->surfaceType() == QWindow::OpenGLSurface)
        setFlag(OpenGLSurface);
    QWindowsContext::instance()->addWindow(m_data.hwnd, this);
    if (aWindow->isTopLevel()) {
        switch (aWindow->windowType()) {
        case Qt::Window:
        case Qt::Dialog:
        case Qt::Sheet:
        case Qt::Drawer:
        case Qt::Popup:
        case Qt::Tool:
            registerDropSite();
            break;
        default:
            break;
        }
    }
}

QWindowsWindow::~QWindowsWindow()
{
    destroyWindow();
}

void QWindowsWindow::destroyWindow()
{
    if (QWindowsContext::verboseIntegration || QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << this << window() << m_data.hwnd;
    if (m_data.hwnd) { // Stop event dispatching before Window is destroyed.
        unregisterDropSite();
        QWindowsContext::instance()->removeWindow(m_data.hwnd);
#ifdef QT_OPENGL_ES_2
        if (m_eglSurface) {
            if (QWindowsContext::verboseGL)
                qDebug("%s: Freeing EGL surface %p, this = %p",
                       __FUNCTION__, m_eglSurface, this);
            eglDestroySurface(m_staticEglContext->display(), m_eglSurface);
            m_eglSurface = 0;
        }
#endif
        if (m_data.hwnd != GetDesktopWindow())
            DestroyWindow(m_data.hwnd);
        m_data.hwnd = 0;
    }
}

void QWindowsWindow::registerDropSite()
{
    if (m_data.hwnd && !m_dropTarget) {
        m_dropTarget = new QWindowsOleDropTarget(window());
        RegisterDragDrop(m_data.hwnd, m_dropTarget);
        CoLockObjectExternal(m_dropTarget, true, true);
    }
}

void QWindowsWindow::unregisterDropSite()
{
    if (m_data.hwnd && m_dropTarget) {
        m_dropTarget->Release();
        CoLockObjectExternal(m_dropTarget, false, true);
        RevokeDragDrop(m_data.hwnd);
        m_dropTarget = 0;
    }
}

QWindow *QWindowsWindow::topLevelOf(QWindow *w)
{
    while (QWindow *parent = w->parent())
        w = parent;
    return w;
}

QWindowsWindow::WindowData
    QWindowsWindow::WindowData::create(const QWindow *w,
                                       const WindowData &parameters,
                                       const QString &title)
{
    WindowCreationData creationData;
    creationData.fromWindow(w, parameters.flags);
    WindowData result = creationData.create(w, parameters.geometry, title);
    creationData.initialize(result.hwnd, false, 1);
    return result;
}

void QWindowsWindow::setVisible(bool visible)
{
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << this << window() << m_data.hwnd << visible;
    if (m_data.hwnd) {
        if (visible) {
            show_sys();
            QWindowSystemInterface::handleSynchronousExposeEvent(window(),
                                                                 QRect(QPoint(), geometry().size()));
        } else {
            hide_sys();
            QWindowSystemInterface::handleSynchronousExposeEvent(window(), QRegion());
        }
    }
}

bool QWindowsWindow::isVisible() const
{
    return m_data.hwnd && IsWindowVisible(m_data.hwnd);
}

bool QWindowsWindow::isActive() const
{
    // Check for native windows or children of the active native window.
    if (const HWND activeHwnd = GetActiveWindow())
        if (m_data.hwnd == activeHwnd || IsChild(activeHwnd, m_data.hwnd))
            return true;
    return false;
}

// partially from QWidgetPrivate::show_sys()
void QWindowsWindow::show_sys() const
{
    int sm = SW_SHOWNORMAL;
    bool fakedMaximize = false;
    const QWindow *w = window();
    const Qt::WindowFlags flags = w->windowFlags();
    const Qt::WindowType type = w->windowType();
    if (w->isTopLevel()) {
        const Qt::WindowState state = w->windowState();
        if (state & Qt::WindowMinimized) {
            sm = SW_SHOWMINIMIZED;
            if (!isVisible())
                sm = SW_SHOWMINNOACTIVE;
        } else if (state & Qt::WindowMaximized) {
            sm = SW_SHOWMAXIMIZED;
            // Windows will not behave correctly when we try to maximize a window which does not
            // have minimize nor maximize buttons in the window frame. Windows would then ignore
            // non-available geometry, and rather maximize the widget to the full screen, minus the
            // window frame (caption). So, we do a trick here, by adding a maximize button before
            // maximizing the widget, and then remove the maximize button afterwards.
            if (flags & Qt::WindowTitleHint &&
                !(flags & (Qt::WindowMinMaxButtonsHint | Qt::FramelessWindowHint))) {
                fakedMaximize = TRUE;
                setStyle(style() | WS_MAXIMIZEBOX);
            }
        }
    }
    if (type == Qt::Popup || type == Qt::ToolTip || type == Qt::Tool)
        sm = SW_SHOWNOACTIVATE;

    ShowWindow(m_data.hwnd, sm);

    if (fakedMaximize) {
        setStyle(style() & ~WS_MAXIMIZEBOX);
        SetWindowPos(m_data.hwnd, 0, 0, 0, 0, 0,
                     SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER
                     | SWP_FRAMECHANGED);
    }
}

// partially from QWidgetPrivate::hide_sys()
void QWindowsWindow::hide_sys() const
{
    const Qt::WindowFlags flags = window()->windowFlags();
    if (flags != Qt::Desktop) {
        if (flags & Qt::Popup)
            ShowWindow(m_data.hwnd, SW_HIDE);
        else
            SetWindowPos(m_data.hwnd,0, 0,0,0,0, SWP_HIDEWINDOW|SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER);
    }
}

void QWindowsWindow::setParent(const QPlatformWindow *newParent)
{
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << window() << newParent;

    if (newParent != parent() && m_data.hwnd)
        setParent_sys(newParent);
}

void QWindowsWindow::setParent_sys(const QPlatformWindow *parent) const
{
    HWND parentHWND = 0;
    if (parent) {
        const QWindowsWindow *parentW = static_cast<const QWindowsWindow *>(parent);
        parentHWND = parentW->handle();

    }

    const bool wasTopLevel = window()->isTopLevel();
    const bool isTopLevel = parentHWND == 0;

    setFlag(WithinSetParent);
    SetParent(m_data.hwnd, parentHWND);
    clearFlag(WithinSetParent);

    // WS_CHILD/WS_POPUP must be manually set/cleared in addition
    // to dialog frames, etc (see  SetParent() ) if the top level state changes.
    if (wasTopLevel != isTopLevel) {
        const unsigned flags = isTopLevel ? unsigned(0) : unsigned(WindowCreationData::ForceChild);
        setWindowFlags_sys(window()->windowFlags(), flags);
    }
}

void QWindowsWindow::handleShown()
{
    QWindowSystemInterface::handleExposeEvent(window(), QRect(QPoint(0, 0), geometry().size()));
}

void QWindowsWindow::handleHidden()
{
    QWindowSystemInterface::handleExposeEvent(window(), QRegion());
}

void QWindowsWindow::setGeometry(const QRect &rectIn)
{
    QRect rect = rectIn;
    // This means it is a call from QWindow::setFramePos() and
    // the coordinates include the frame (size is still the contents rectangle).
    if (QWindowsGeometryHint::positionIncludesFrame(window())) {
        const QMargins margins = frameMargins();
        rect.moveTopLeft(rect.topLeft() + QPoint(margins.left(), margins.top()));
    }
    const QSize oldSize = m_data.geometry.size();
    m_data.geometry = rect;
    const QSize newSize = rect.size();
    // Check on hint.
    if (newSize != oldSize) {
        const QWindowsGeometryHint hint(window());
        if (!hint.validSize(newSize)) {
            qWarning("%s: Attempt to set a size (%dx%d) violating the constraints"
                     "(%dx%d - %dx%d) on window '%s'.", __FUNCTION__,
                     newSize.width(), newSize.height(),
                     hint.minimumSize.width(), hint.minimumSize.height(),
                     hint.maximumSize.width(), hint.maximumSize.height(),
                     qPrintable(window()->objectName()));
        }
    }
    if (m_data.hwnd) {
        // A ResizeEvent with resulting geometry will be sent. If we cannot
        // achieve that size (for example, window title minimal constraint),
        // notify and warn.
        setGeometry_sys(rect);
        if (m_data.geometry != rect) {
            qWarning("%s: Unable to set geometry %dx%d+%d+%d on '%s'."
                     " Resulting geometry:  %dx%d+%d+%d "
                     "(frame: %d, %d, %d, %d).",
                     __FUNCTION__,
                     rect.width(), rect.height(), rect.x(), rect.y(),
                     qPrintable(window()->objectName()),
                     m_data.geometry.width(), m_data.geometry.height(),
                     m_data.geometry.x(), m_data.geometry.y(),
                     m_data.frame.left(), m_data.frame.top(),
                     m_data.frame.right(), m_data.frame.bottom());
        }
    } else {
        QPlatformWindow::setGeometry(rect);
    }
}

void QWindowsWindow::handleMoved()
{
    // Minimize/Set parent can send nonsensical move events.
    if (!IsIconic(m_data.hwnd) && !testFlag(WithinSetParent))
        handleGeometryChange();
}

void QWindowsWindow::handleResized(int wParam)
{
    switch (wParam) {
    case SIZE_MAXHIDE: // Some other window affected.
    case SIZE_MAXSHOW:
        return;
    case SIZE_MINIMIZED:
        handleWindowStateChange(Qt::WindowMinimized);
        return;
    case SIZE_MAXIMIZED:
        handleWindowStateChange(Qt::WindowMaximized);
        handleGeometryChange();
        break;
    case SIZE_RESTORED:
        bool fullScreen = isFullScreen_sys();
        if ((m_windowState != Qt::WindowNoState) || fullScreen)
            handleWindowStateChange(fullScreen ? Qt::WindowFullScreen : Qt::WindowNoState);
        handleGeometryChange();
        break;
    }
}

void QWindowsWindow::handleGeometryChange()
{
    m_data.geometry = geometry_sys();
    QPlatformWindow::setGeometry(m_data.geometry);
    if (testFlag(SynchronousGeometryChangeEvent))
        QWindowSystemInterface::handleSynchronousGeometryChange(window(), m_data.geometry);
    else
        QWindowSystemInterface::handleGeometryChange(window(), m_data.geometry);

    if (QWindowsContext::verboseEvents || QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << this << window() << m_data.geometry;
}

void QWindowsWindow::setGeometry_sys(const QRect &rect) const
{
    const QMargins margins = frameMargins();
    const QRect frameGeometry = rect + margins;

    if (QWindowsContext::verboseWindows)
        qDebug() << '>' << __FUNCTION__ << this << window()
                 << "    \n from " << geometry_sys() << " frame: "
                 << margins << " to " <<rect
                 << " new frame: " << frameGeometry;

    const bool rc = MoveWindow(m_data.hwnd, frameGeometry.x(), frameGeometry.y(),
                               frameGeometry.width(), frameGeometry.height(), true);
    if (QWindowsContext::verboseWindows)
        qDebug() << '<' << __FUNCTION__ << this << window()
                 << "    \n resulting " << rc << geometry_sys();
}

QRect QWindowsWindow::frameGeometry_sys() const
{
    // Warning: Returns bogus values when minimized.
    bool isRealTopLevel = window()->isTopLevel() && !m_data.embedded;
    return frameGeometry(m_data.hwnd, isRealTopLevel);
}

QRect QWindowsWindow::geometry_sys() const
{
    return frameGeometry_sys() - frameMargins();
}

/*!
    Allocates a HDC for the window or returns the temporary one
    obtained from WinAPI BeginPaint within a WM_PAINT event.

    \sa releaseDC()
*/

HDC QWindowsWindow::getDC()
{
    if (!m_hdc)
        m_hdc = GetDC(handle());
    return m_hdc;
}

/*!
    Relases the HDC for the window or does nothing in
    case it was obtained from WinAPI BeginPaint within a WM_PAINT event.

    \sa getDC()
*/

void QWindowsWindow::releaseDC()
{
    if (m_hdc && !testFlag(DCFromBeginPaint)) {
        ReleaseDC(handle(), m_hdc);
        m_hdc = 0;
    }
}

bool QWindowsWindow::handleWmPaint(HWND hwnd, UINT message,
                                         WPARAM, LPARAM)
{
    // Ignore invalid update bounding rectangles
    if (!GetUpdateRect(m_data.hwnd, 0, FALSE))
        return false;
    if (message == WM_ERASEBKGND) // Backing store - ignored.
        return true;
    PAINTSTRUCT ps;
    if (testFlag(OpenGLSurface)) {
        // Observed painting problems with Aero style disabled (QTBUG-7865).
        if (testFlag(OpenGLDoubleBuffered))
            InvalidateRect(hwnd, 0, false);
        BeginPaint(hwnd, &ps);
        QWindowSystemInterface::handleSynchronousExposeEvent(window(),
                                                             QRegion(qrectFromRECT(ps.rcPaint)));
        EndPaint(hwnd, &ps);
    } else {
        const HDC dc = BeginPaint(hwnd, &ps);
        const QRect updateRect = qrectFromRECT(ps.rcPaint);
        if (updateRect.size() == m_data.geometry.size()) {
            // Store DC for access by the backing store if it has the full size.
            releaseDC();
            setFlag(DCFromBeginPaint);
            m_hdc = dc;
        }
        if (QWindowsContext::verboseIntegration)
            qDebug() << __FUNCTION__ << this << window() << updateRect;

        QWindowSystemInterface::handleSynchronousExposeEvent(window(), QRegion(updateRect));
        if (testFlag(DCFromBeginPaint)) {
            clearFlag(DCFromBeginPaint);
            m_hdc = 0;
        }
        EndPaint(hwnd, &ps);
    }
    return true;
}

void QWindowsWindow::setWindowTitle(const QString &title)
{
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << this << window() <<title;
    if (m_data.hwnd)
        SetWindowText(m_data.hwnd, (const wchar_t*)title.utf16());
}

Qt::WindowFlags QWindowsWindow::setWindowFlags(Qt::WindowFlags flags)
{
    if (QWindowsContext::verboseWindows)
        qDebug() << '>' << __FUNCTION__ << this << window() << "\n    from: "
                 << QWindowsWindow::debugWindowFlags(m_data.flags)
                 << "\n    to: " << QWindowsWindow::debugWindowFlags(flags);
    const QRect oldGeometry = geometry();
    if (m_data.flags != flags) {
        m_data.flags = flags;
        if (m_data.hwnd)
            m_data = setWindowFlags_sys(flags);
    }
    // When switching to a frameless window, geometry
    // may change without a WM_MOVE. Report change manually.
    // Do not send synchronously as not to clobber the widget
    // geometry in a sequence of setting flags and geometry.
    const QRect newGeometry = geometry_sys();
    if (oldGeometry != newGeometry)
        handleGeometryChange();

    if (QWindowsContext::verboseWindows)
        qDebug() << '<' << __FUNCTION__ << "\n    returns: "
                 << QWindowsWindow::debugWindowFlags(m_data.flags)
                 << " geometry " << oldGeometry << "->" << newGeometry;
    return m_data.flags;
}

QWindowsWindow::WindowData QWindowsWindow::setWindowFlags_sys(Qt::WindowFlags wt,
                                                              unsigned flags) const
{
    WindowCreationData creationData;
    creationData.fromWindow(window(), wt, flags);
    creationData.applyWindowFlags(m_data.hwnd);
    creationData.initialize(m_data.hwnd, true, m_opacity);

    WindowData result = m_data;
    result.flags = creationData.flags;
    result.embedded = creationData.embedded;
    setFlag(FrameDirty);
    return result;
}

void QWindowsWindow::handleWindowStateChange(Qt::WindowState state)
{
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << this << window()
                 << "\n    from " << debugWindowStates(m_windowState)
                 << " to " << debugWindowStates(state);
    setFlag(FrameDirty);
    m_windowState = state;
    QWindowSystemInterface::handleWindowStateChanged(window(), state);
}

Qt::WindowState QWindowsWindow::setWindowState(Qt::WindowState state)
{
    if (m_data.hwnd) {
        setWindowState_sys(state);
        m_windowState = state;
    }
    return state;
}

bool QWindowsWindow::isFullScreen_sys() const
{
    return geometry_sys() == window()->screen()->geometry();
}

/*!
    \brief Change the window state.

    \note Window frames change when maximized;
    the top margin shrinks somewhat but that cannot be obtained using
    AdjustWindowRectEx().

    \note Some calls to SetWindowLong require a subsequent call
    to ShowWindow.
*/

void QWindowsWindow::setWindowState_sys(Qt::WindowState newState)
{
    const Qt::WindowStates oldStates = m_windowState;
    // Maintain the active flag as the platform window API does not
    // use it.
    Qt::WindowStates newStates = newState;
    if (isActive())
        newStates |=  Qt::WindowActive;
    if (oldStates == newStates)
        return;
    if (QWindowsContext::verboseWindows)
        qDebug() << '>' << __FUNCTION__ << this << window()
                 << " from " << debugWindowStates(oldStates)
                 << " to " << debugWindowStates(newStates);

    const bool isActive = newStates & Qt::WindowActive;
    const int max    = isActive ? SW_SHOWMAXIMIZED : SW_MAXIMIZE;
    const int normal = isActive ? SW_SHOWNORMAL    : SW_SHOWNOACTIVATE;
    const int min    = isActive ? SW_SHOWMINIMIZED : SW_MINIMIZE;
    const bool visible = isVisible();

    setFlag(FrameDirty);

    if ((oldStates & Qt::WindowMaximized) != (newStates & Qt::WindowMaximized)) {
        if (visible && !(newStates & Qt::WindowMinimized))
            ShowWindow(m_data.hwnd, (newStates & Qt::WindowMaximized) ? max : normal);
    }

    if ((oldStates & Qt::WindowFullScreen) != (newStates & Qt::WindowFullScreen)) {
        if (newStates & Qt::WindowFullScreen) {
#ifndef Q_FLATTEN_EXPOSE
            UINT newStyle = WS_CLIPCHILDREN | WS_CLIPSIBLINGS | WS_POPUP;
#else
            UINT newStyle = WS_POPUP;
#endif
            // Save geometry and style to be restored when fullscreen
            // is turned off again, since on Windows, it is not a real
            // Window state but emulated by changing geometry and style.
            if (!m_savedStyle) {
                m_savedStyle = style();
#ifndef Q_OS_WINCE
                if (oldStates & Qt::WindowMinimized) {
                    WINDOWPLACEMENT wp;
                    wp.length = sizeof(WINDOWPLACEMENT);
                    if (GetWindowPlacement(m_data.hwnd, &wp))
                        m_savedFrameGeometry = qrectFromRECT(wp.rcNormalPosition);
                } else {
#endif
                    m_savedFrameGeometry = frameGeometry_sys();
#ifndef Q_OS_WINCE
                }
#endif
            }
            if (m_savedStyle & WS_SYSMENU)
                newStyle |= WS_SYSMENU;
            if (visible)
                newStyle |= WS_VISIBLE;
            setStyle(newStyle);

            const QRect r = window()->screen()->geometry();
            UINT swpf = SWP_FRAMECHANGED;
            if (newStates & Qt::WindowActive)
                swpf |= SWP_NOACTIVATE;
            const bool wasSync = testFlag(SynchronousGeometryChangeEvent);
            setFlag(SynchronousGeometryChangeEvent);
            SetWindowPos(m_data.hwnd, HWND_TOP, r.left(), r.top(), r.width(), r.height(), swpf);
            if (!wasSync)
                clearFlag(SynchronousGeometryChangeEvent);
            QWindowSystemInterface::handleSynchronousGeometryChange(window(), r);
        } else if (!(newStates & Qt::WindowMinimized)) {
            // Restore saved state.
            unsigned newStyle = m_savedStyle ? m_savedStyle : style();
            if (visible)
                newStyle |= WS_VISIBLE;
            setStyle(newStyle);

            UINT swpf = SWP_FRAMECHANGED | SWP_NOZORDER;
            if (newStates & Qt::WindowActive)
                swpf |= SWP_NOACTIVATE;
            if (!m_savedFrameGeometry.isValid())
                swpf |= SWP_NOSIZE | SWP_NOMOVE;
            const bool wasSync = testFlag(SynchronousGeometryChangeEvent);
            setFlag(SynchronousGeometryChangeEvent);
            SetWindowPos(m_data.hwnd, 0, m_savedFrameGeometry.x(), m_savedFrameGeometry.y(),
                         m_savedFrameGeometry.width(), m_savedFrameGeometry.height(), swpf);
            if (!wasSync)
                clearFlag(SynchronousGeometryChangeEvent);
            // preserve maximized state
            if (visible)
                ShowWindow(m_data.hwnd, (newStates & Qt::WindowMaximized) ? max : normal);
            m_savedStyle = 0;
            m_savedFrameGeometry = QRect();
        }
    }

    if ((oldStates & Qt::WindowMinimized) != (newStates & Qt::WindowMinimized)) {
        if (visible)
            ShowWindow(m_data.hwnd, (newStates & Qt::WindowMinimized) ? min :
                       (newStates & Qt::WindowMaximized) ? max : normal);
    }
    if (QWindowsContext::verboseWindows)
        qDebug() << '<' << __FUNCTION__ << this << window()
                 << debugWindowStates(newStates);
}

void QWindowsWindow::setStyle(unsigned s) const
{
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << this << window() << debugWinStyle(s);
    setFlag(FrameDirty);
    SetWindowLongPtr(m_data.hwnd, GWL_STYLE, s);
}

void QWindowsWindow::setExStyle(unsigned s) const
{
    if (QWindowsContext::verboseWindows)
        qDebug().nospace() << __FUNCTION__ << ' ' << this << ' ' << window()
        << " 0x" << QByteArray::number(s, 16);
    setFlag(FrameDirty);
    SetWindowLongPtr(m_data.hwnd, GWL_EXSTYLE, s);
}

void QWindowsWindow::raise()
{
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << this << window();
    SetWindowPos(m_data.hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
}

void QWindowsWindow::lower()
{
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << this << window();
    if (m_data.hwnd)
        SetWindowPos(m_data.hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
}

void QWindowsWindow::windowEvent(QEvent *event)
{
    switch (event->type()) {
    case QEvent::WindowBlocked: // Blocked by another modal window.
        setEnabled(false);
        setFlag(BlockedByModal);
        break;
    case QEvent::WindowUnblocked:
        setEnabled(true);
        clearFlag(BlockedByModal);
        break;
    default:
        break;
    }
}

void QWindowsWindow::propagateSizeHints()
{
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << this << window();
}

QMargins QWindowsWindow::frameMargins() const
{
    // Frames are invalidated by style changes (window state, flags).
    // As they are also required for geometry calculations in resize
    // event sequences, introduce a dirty flag mechanism to be able
    // to cache results.
    if (testFlag(FrameDirty)) {
        m_data.frame = QWindowsGeometryHint::frame(style(), exStyle());
        clearFlag(FrameDirty);
    }
    return m_data.frame;
}

void QWindowsWindow::setOpacity(qreal level)
{
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << level;
    if (m_opacity != level) {
        m_opacity = level;
        if (m_data.hwnd)
            setWindowOpacity(m_data.hwnd, m_data.flags, level);
    }
}

static inline HRGN createRectRegion(const QRect &r)
{
    return CreateRectRgn(r.left(), r.top(), r.x() + r.width(), r.y() + r.height());
}

static inline void addRectToWinRegion(const QRect &rect, HRGN *winRegion)
{
    if (const HRGN rectRegion = createRectRegion(rect)) {
        HRGN result = CreateRectRgn(0, 0, 0, 0);
        if (CombineRgn(result, *winRegion, rectRegion, RGN_OR)) {
            DeleteObject(winRegion);
            *winRegion = result;
        }
        DeleteObject(rectRegion);
    }
}

static HRGN qRegionToWinRegion(const QRegion &region)
{
    const QVector<QRect> rects = region.rects();
    if (rects.isEmpty())
        return NULL;
    const int rectCount = rects.size();
    if (rectCount == 1)
        return createRectRegion(region.boundingRect());
    HRGN hRegion = createRectRegion(rects.front());
    for (int i = 1; i < rectCount; ++i)
        addRectToWinRegion(rects.at(i), &hRegion);
    return hRegion;
}

void QWindowsWindow::setMask(const QRegion &region)
{
    if (region.isEmpty()) {
         SetWindowRgn(m_data.hwnd, 0, true);
         return;
    }
    const HRGN winRegion = qRegionToWinRegion(region);

    // Mask is in client area coordinates, so offset it in case we have a frame
    if (window()->isTopLevel()) {
        const QMargins margins = frameMargins();
        OffsetRgn(winRegion, margins.left(), margins.top());
    }

    // SetWindowRgn takes ownership.
    if (!SetWindowRgn(m_data.hwnd, winRegion, true))
        DeleteObject(winRegion);
}

void QWindowsWindow::requestActivateWindow()
{
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << this << window();
    // 'Active' state handling is based in focus since it needs to work for
    // child windows as well.
    if (m_data.hwnd) {
        SetForegroundWindow(m_data.hwnd);
        SetFocus(m_data.hwnd);
    }
}

bool QWindowsWindow::setKeyboardGrabEnabled(bool grab)
{
    if (!m_data.hwnd) {
        qWarning("%s: No handle", __FUNCTION__);
        return false;
    }
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << this << window() << grab;

    QWindowsContext *context = QWindowsContext::instance();
    if (grab) {
        context->setKeyGrabber(window());
    } else {
        if (context->keyGrabber() == window())
            context->setKeyGrabber(0);
    }
    return true;
}

bool QWindowsWindow::setMouseGrabEnabled(bool grab)
{
    bool result = false;
    if (!m_data.hwnd) {
        qWarning("%s: No handle", __FUNCTION__);
        return result;
    }
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << window() << grab;

    if (m_mouseGrab != grab) {
        m_mouseGrab = grab;
        if (isVisible())
            setMouseGrabEnabled_sys(grab);
    }
    return grab;
}

void QWindowsWindow::setMouseGrabEnabled_sys(bool grab)
{
    if (grab) {
        SetCapture(m_data.hwnd);
    } else {
        ReleaseCapture();
    }
}

static inline DWORD cornerToWinOrientation(Qt::Corner corner)
{
    switch (corner) {
    case Qt::TopLeftCorner:
        return 0xf004; // SZ_SIZETOPLEFT;
    case Qt::TopRightCorner:
        return 0xf005; // SZ_SIZETOPRIGHT
    case Qt::BottomLeftCorner:
        return 0xf007; // SZ_SIZEBOTTOMLEFT
    case Qt::BottomRightCorner:
        return 0xf008; // SZ_SIZEBOTTOMRIGHT
    }
    return 0;
}

bool QWindowsWindow::startSystemResize(const QPoint &, Qt::Corner corner)
{
    if (!GetSystemMenu(m_data.hwnd, FALSE))
        return false;

    ReleaseCapture();
    PostMessage(m_data.hwnd, WM_SYSCOMMAND, cornerToWinOrientation(corner), 0);
    setFlag(SizeGripOperation);
    return true;
}

void QWindowsWindow::setFrameStrutEventsEnabled(bool enabled)
{
    if (enabled) {
        setFlag(FrameStrutEventsEnabled);
    } else {
        clearFlag(FrameStrutEventsEnabled);
    }
}

#ifndef Q_OS_WINCE // maybe available on some SDKs revisit WM_GETMINMAXINFO
void QWindowsWindow::getSizeHints(MINMAXINFO *mmi) const
{
    const QWindowsGeometryHint hint(window());
    hint.applyToMinMaxInfo(m_data.hwnd, mmi);
    if (QWindowsContext::verboseWindows)
        qDebug() << __FUNCTION__ << window() << *mmi;
}
#endif // !Q_OS_WINCE

/*!
    \brief Applies to cursor property set on the window to the global cursor.

    \sa QWindowsCursor
*/

void QWindowsWindow::applyCursor()
{
    SetCursor(m_cursor.handle());
}

void QWindowsWindow::setCursor(const QWindowsWindowCursor &c)
{
    if (c.handle() != m_cursor.handle()) {
        const bool underMouse = QWindowsContext::instance()->windowUnderMouse() == window();
        if (QWindowsContext::verboseWindows)
            qDebug() << window() << __FUNCTION__ << "Shape=" << c.cursor().shape()
                     << " isWUM=" << underMouse;
        m_cursor = c;
        if (underMouse)
            applyCursor();
    }
}

/*!
    \brief Find a child window using flags from  ChildWindowFromPointEx.
*/

QWindowsWindow *QWindowsWindow::childAtScreenPoint(const QPoint &screenPoint,
                                                           unsigned cwexflags) const
{
    if (m_data.hwnd)
        return QWindowsContext::instance()->findPlatformWindowAt(m_data.hwnd, screenPoint, cwexflags);
    return 0;
}

QWindowsWindow *QWindowsWindow::childAt(const QPoint &clientPoint, unsigned cwexflags) const
{
    if (m_data.hwnd)
        return childAtScreenPoint(QWindowsGeometryHint::mapToGlobal(m_data.hwnd, clientPoint),
                                  cwexflags);
    return 0;
}

#ifndef Q_OS_WINCE
void QWindowsWindow::alertWindow(int durationMs)
{
    DWORD timeOutMs = GetCaretBlinkTime();
    if (!timeOutMs || timeOutMs == INFINITE)
        timeOutMs = 250;

    FLASHWINFO info;
    info.cbSize = sizeof(info);
    info.hwnd = m_data.hwnd;
    info.dwFlags = FLASHW_TRAY;
    info.dwTimeout = timeOutMs;
    info.uCount = durationMs == 0 ? 10 : durationMs / timeOutMs;
    FlashWindowEx(&info);
}

void QWindowsWindow::stopAlertWindow()
{
    FLASHWINFO info;
    info.cbSize = sizeof(info);
    info.hwnd = m_data.hwnd;
    info.dwFlags = FLASHW_STOP;
    info.dwTimeout = 0;
    info.uCount = 0;
    FlashWindowEx(&info);
}
#endif // !Q_OS_WINCE

bool QWindowsWindow::isEnabled() const
{
    return (style() & WS_DISABLED) == 0;
}

void QWindowsWindow::setEnabled(bool enabled)
{
    const unsigned oldStyle = style();
    unsigned newStyle = oldStyle;
    if (enabled) {
        newStyle &= ~WS_DISABLED;
    } else {
        newStyle |= WS_DISABLED;
    }
    if (newStyle != oldStyle)
        setStyle(newStyle);
}

#ifdef QT_OPENGL_ES_2
EGLSurface QWindowsWindow::ensureEglSurfaceHandle(const QWindowsWindow::QWindowsEGLStaticContextPtr &staticContext, EGLConfig config)
{
    if (!m_eglSurface) {
        m_staticEglContext = staticContext;
        m_eglSurface = eglCreateWindowSurface(staticContext->display(), config, (EGLNativeWindowType)m_data.hwnd, NULL);
        if (m_eglSurface == EGL_NO_SURFACE)
            qWarning("%s: Could not create the egl surface (eglCreateWindowSurface failed): error = 0x%x\n",
                     Q_FUNC_INFO, eglGetError());
        if (QWindowsContext::verboseGL)
            qDebug("%s: Created EGL surface %p, this = %p",
                   __FUNCTION__, m_eglSurface, this);
    }
    return m_eglSurface;
}
#endif // QT_OPENGL_ES_2

QByteArray QWindowsWindow::debugWindowFlags(Qt::WindowFlags wf)
{
    const int iwf = int(wf);
    QByteArray rc = "0x";
    rc += QByteArray::number(iwf, 16);
    rc += " [";

    switch ((iwf & Qt::WindowType_Mask)) {
    case Qt::Widget:
        rc += " Widget";
        break;
    case Qt::Window:
        rc += " Window";
        break;
    case Qt::Dialog:
        rc += " Dialog";
        break;
    case Qt::Sheet:
        rc += " Sheet";
        break;
    case Qt::Popup:
        rc += " Popup";
        break;
    case Qt::Tool:
        rc += " Tool";
        break;
    case Qt::ToolTip:
        rc += " ToolTip";
        break;
    case Qt::SplashScreen:
        rc += " SplashScreen";
        break;
    case Qt::Desktop:
        rc += " Desktop";
        break;
    case Qt::SubWindow:
        rc += " SubWindow";
        break;
    }
    if (iwf & Qt::MSWindowsFixedSizeDialogHint) rc += " MSWindowsFixedSizeDialogHint";
    if (iwf & Qt::MSWindowsOwnDC) rc += " MSWindowsOwnDC";
    if (iwf & Qt::FramelessWindowHint) rc += " FramelessWindowHint";
    if (iwf & Qt::WindowTitleHint) rc += " WindowTitleHint";
    if (iwf & Qt::WindowSystemMenuHint) rc += " WindowSystemMenuHint";
    if (iwf & Qt::WindowMinimizeButtonHint) rc += " WindowMinimizeButtonHint";
    if (iwf & Qt::WindowMaximizeButtonHint) rc += " WindowMaximizeButtonHint";
    if (iwf & Qt::WindowContextHelpButtonHint) rc += " WindowContextHelpButtonHint";
    if (iwf & Qt::WindowShadeButtonHint) rc += " WindowShadeButtonHint";
    if (iwf & Qt::WindowStaysOnTopHint) rc += " WindowStaysOnTopHint";
    if (iwf & Qt::CustomizeWindowHint) rc += " CustomizeWindowHint";
    if (iwf & Qt::WindowStaysOnBottomHint) rc += " WindowStaysOnBottomHint";
    if (iwf & Qt::WindowCloseButtonHint) rc += " WindowCloseButtonHint";
    rc += ']';
    return rc;
}

QT_END_NAMESPACE
