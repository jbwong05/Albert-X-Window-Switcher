#include <QDebug>
#include <QPointer>
#include <stdexcept>
#include "albert/util/standarditem.h"
#include "xdg/iconlookup.h"
#include "configwidget.h"
#include "extension.h"

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
            char *title_utf8 = get_window_title(d->display, clientList[i]);
            QString windowTitle(title_utf8);

            if(title_utf8 != NULL) {
                free(title_utf8);
            } else {
                windowTitle = "";
            }

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

        if(clientList != NULL) {
            free(clientList);
        }
    }
}

Window * XWindowSwitcher::Extension::getClientList(Display *display, unsigned long *size) const {
    Window *clientList;
    char netClientList[] = "_NET_CLIENT_LIST";

    if ((clientList = (Window *)get_property(display, DefaultRootWindow(display), 
                    XA_WINDOW, netClientList, size)) == NULL) {
        return NULL;
    }

    return clientList;
}

char * XWindowSwitcher::Extension::get_property(Display *disp, Window win,
        Atom xa_prop_type, char *prop_name, unsigned long *size) const {
    Atom xa_prop_name;
    Atom xa_ret_type;
    int ret_format;
    unsigned long ret_nitems;
    unsigned long ret_bytes_after;
    unsigned long tmp_size;
    unsigned char *ret_prop;
    char *ret;
    
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
    ret = (char *)malloc(tmp_size + 1);

    if(ret == NULL) {
        return NULL;
    }

    memcpy(ret, ret_prop, tmp_size);
    ret[tmp_size] = '\0';

    if(size) {
        *size = tmp_size;
    }
    
    XFree(ret_prop);
    return ret;
}

char * XWindowSwitcher::Extension::get_window_title(Display *disp, Window win) const {
    char *title;
    char *net_wm_name;
    char utf8[] = "UTF8_STRING";
    char netWMName[] = "_NET_WM_NAME";
  
    net_wm_name = get_property(disp, win, XInternAtom(disp, utf8, False), netWMName, NULL);

    if (net_wm_name) {
        title = strdup(net_wm_name);
        free(net_wm_name);
        return title;

    } else {
        XTextProperty textProperty;
        if(XGetWMName(disp, win, &textProperty)) {
            char *converted = reinterpret_cast<char*>(textProperty.value);
            title = strdup(converted);
            return title;
        } else {
            return NULL;
        }
    }
}

XWindowSwitcher::ActivateWindowAction::ActivateWindowAction(const QString &text, Display *display, Window window)
    : StandardActionBase(text), display(display), window(window) {

}

void XWindowSwitcher::ActivateWindowAction::activate() {
    if(display != NULL) {
        char netActiveWindow[] = "_NET_ACTIVE_WINDOW";
        client_msg(netActiveWindow);
        XMapRaised(display, window);
        XSetInputFocus(display, window, RevertToNone, CurrentTime);
        XSync(display, false);
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