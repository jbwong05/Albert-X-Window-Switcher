#include <QDebug>
#include <QPointer>
#include <stdexcept>
#include "albert/util/standarditem.h"
#include "xdg/iconlookup.h"
#include "configwidget.h"
#include "extension.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <glib.h>

using namespace Core;
using namespace std;

#define MAX_PROPERTY_VALUE_LEN 4096
#define FALLBACK_ICON "preferences-system"

class XWindowSwitcher::Private {
    public:
        QPointer<ConfigWidget> widget;
        Display *display;
        QMap<QString, QString> iconPaths;
        QString fallbackIconPath;
};


/** ***************************************************************************/
XWindowSwitcher::Extension::Extension() : Core::Extension("org.albert.extension.xwindowswitcher"), Core::QueryHandler(Core::Plugin::id()), d(new Private) {
    registerQueryHandler(this);

    d->fallbackIconPath = XDG::IconLookup::iconPath(FALLBACK_ICON);

    d->display = XOpenDisplay(NULL);
    if(d->display == NULL) {
        qDebug() << "Cannot open display";

    } else {
        Window *clientList;
        unsigned long clientListSize;

        if((clientList = getClientList(d->display, &clientListSize)) == NULL) {
            qDebug() << "No windows found";
            return; 
        }

        clientListSize /= sizeof(Window);
        for(long unsigned int i = 0; i < clientListSize; i++) {
            XClassHint classHint;
            XGetClassHint(d->display, clientList[i], &classHint);
            QString applicationName(classHint.res_name);

            QString path = XDG::IconLookup::iconPath(classHint.res_name);
            if(path.isEmpty()) {
                qDebug() << "No icon for " << applicationName << "found";
                d->iconPaths.insert(applicationName, d->fallbackIconPath);
            } else {
                d->iconPaths.insert(applicationName, path);
            }
        }
    }
}



/** ***************************************************************************/
XWindowSwitcher::Extension::~Extension() {
    if(d->display != NULL) {
        XCloseDisplay(d->display);
    }
}



/** ***************************************************************************/
QWidget *XWindowSwitcher::Extension::widget(QWidget *parent) {
    if(d->widget.isNull()) {
        d->widget = new ConfigWidget(parent);
    }
    return d->widget;
}



/** ***************************************************************************/
void XWindowSwitcher::Extension::setupSession() {

}



/** ***************************************************************************/
void XWindowSwitcher::Extension::teardownSession() {

}



/** ***************************************************************************/
void XWindowSwitcher::Extension::handleQuery(Core::Query *query) const {

    if(d->display != NULL) {

        if(query->string().isEmpty() || query->string().length() <= 1) {
            return;
        }

        Window *clientList;
        unsigned long clientListSize;

        if((clientList = getClientList(d->display, &clientListSize)) == NULL) {
            qDebug() << "No windows found";
            return; 
        }

        clientListSize /= sizeof(Window);
        for(long unsigned int i = 0; i < clientListSize; i++) {
            gchar *title_utf8 = get_window_title(d->display, clientList[i]);
            QString windowTitle(title_utf8);
            g_free(title_utf8);

            XClassHint classHint;
            XGetClassHint(d->display, clientList[i], &classHint);
            QString applicationName(classHint.res_name);
            if(applicationName.contains(query->string()) || windowTitle.contains(query->string())) {
                auto item = make_shared<StandardItem>(applicationName);
                item->setText("Switch Windows");
                item->setSubtext(windowTitle);

                QString iconPath;
                if(d->iconPaths.contains(applicationName)) {
                    iconPath = d->iconPaths[applicationName];
                } else {
                    iconPath = XDG::IconLookup::iconPath(applicationName);

                    if(iconPath.isEmpty()) {
                        d->iconPaths.insert(applicationName, d->fallbackIconPath);
                    } else {
                        d->iconPaths.insert(applicationName, iconPath);
                    }
                }

                item->setIconPath(iconPath);
                item->addAction(make_shared<ActivateWindowAction>(applicationName, d->display, clientList[i]));
                query->addMatch(std::move(item), 0);
            }
        }

        g_free(clientList);
    }
}

Window * XWindowSwitcher::Extension::getClientList(Display *display, unsigned long *size) const {
    Window *clientList;

    if ((clientList = (Window *)get_property(display, DefaultRootWindow(display), 
                    XA_WINDOW, "_NET_CLIENT_LIST", size)) == NULL) {
        return NULL;
    }

    return clientList;
}

gchar * XWindowSwitcher::Extension::get_property(Display *disp, Window win,
        Atom xa_prop_type, gchar *prop_name, unsigned long *size) const {
    Atom xa_prop_name;
    Atom xa_ret_type;
    int ret_format;
    unsigned long ret_nitems;
    unsigned long ret_bytes_after;
    unsigned long tmp_size;
    unsigned char *ret_prop;
    gchar *ret;
    
    xa_prop_name = XInternAtom(disp, prop_name, False);
    
    /* MAX_PROPERTY_VALUE_LEN / 4 explanation (XGetWindowProperty manpage):
     *
     * long_length = Specifies the length in 32-bit multiples of the
     *               data to be retrieved.
     */
    if(XGetWindowProperty(disp, win, xa_prop_name, 0, MAX_PROPERTY_VALUE_LEN / 4, False,
            xa_prop_type, &xa_ret_type, &ret_format,     
            &ret_nitems, &ret_bytes_after, &ret_prop) != Success) {
        return NULL;
    }
  
    if(xa_ret_type != xa_prop_type) {
        XFree(ret_prop);
        return NULL;
    }

    // null terminate the result to make string handling easier 
    tmp_size = (ret_format / 8) * ret_nitems;
    // Correct 64 Architecture implementation of 32 bit data
    if(ret_format==32) {
        tmp_size *= sizeof(long) / 4;
    }
    ret = (gchar *)g_malloc(tmp_size + 1);
    memcpy(ret, ret_prop, tmp_size);
    ret[tmp_size] = '\0';

    if(size) {
        *size = tmp_size;
    }
    
    XFree(ret_prop);
    return ret;
}

gchar * XWindowSwitcher::Extension::get_window_title (Display *disp, Window win) const {
    gchar *title_utf8;
    gchar *wm_name;
    gchar *net_wm_name;

    wm_name = get_property(disp, win, XA_STRING, "WM_NAME", NULL);
    net_wm_name = get_property(disp, win, 
            XInternAtom(disp, "UTF8_STRING", False), "_NET_WM_NAME", NULL);

    if (net_wm_name) {
        title_utf8 = g_strdup(net_wm_name);
    }
    else {
        if (wm_name) {
            title_utf8 = g_locale_to_utf8(wm_name, -1, NULL, NULL, NULL);
        }
        else {
            title_utf8 = NULL;
        }
    }

    g_free(wm_name);
    g_free(net_wm_name);
    
    return title_utf8;
}

XWindowSwitcher::ActivateWindowAction::ActivateWindowAction(const QString &text, Display *display, Window window)
    : StandardActionBase(text), display(display), window(window) {

}

void XWindowSwitcher::ActivateWindowAction::activate() {
    if(display != NULL) {
        client_msg("_NET_ACTIVE_WINDOW");
        XMapRaised(display, window);
        XFlush(display);
    }
}

void XWindowSwitcher::ActivateWindowAction::client_msg(char *msg) {
    XEvent event;
    long mask = SubstructureRedirectMask | SubstructureNotifyMask;

    event.xclient.type = ClientMessage;
    event.xclient.serial = 0;
    event.xclient.send_event = True;
    event.xclient.message_type = XInternAtom(display, msg, False);
    event.xclient.window = window;
    event.xclient.format = 32;
    event.xclient.data.l[0] = 0;
    event.xclient.data.l[1] = 0;
    event.xclient.data.l[2] = 0;
    event.xclient.data.l[3] = 0;
    event.xclient.data.l[4] = 0;

    XSendEvent(display, DefaultRootWindow(display), False, mask, &event);
}