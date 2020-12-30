#include <QDebug>
#include <QPointer>
#include <QDebug>
#include <stdexcept>
#include "albert/util/standarditem.h"
#include "xdg/iconlookup.h"
#include "configwidget.h"
#include "extension.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
//#include <X11/cursorfont.h>
//#include <X11/Xmu/WinUtil.h>
#include <glib.h>

using namespace Core;
using namespace std;

#define MAX_PROPERTY_VALUE_LEN 4096

class XWindowSwitcher::Private {
    public:
        QPointer<ConfigWidget> widget;
        Display *display;
};


/** ***************************************************************************/
XWindowSwitcher::Extension::Extension() : Core::Extension("org.albert.extension.xwindowswitcher"), Core::QueryHandler(Core::Plugin::id()), d(new Private) {
    registerQueryHandler(this);

    d->display = XOpenDisplay(NULL);
    if(d->display == NULL) {
        qDebug() << "Cannot open display";
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
void XWindowSwitcher::Extension::handleQuery(Core::Query *) const {
    // necessary to make g_get_charset() and g_locale_*() work
    //setlocale(LC_ALL, "");

    Window *clientList;
    unsigned long clientListSize;

    if((clientList = getClientList(d->display, &clientListSize)) == NULL) {
        qDebug() << "No windows found";
        return; 
    }

    qDebug() << "Num windows: " << clientListSize;
    for(long unsigned int i = 0; i < clientListSize / sizeof(Window); i++) {
        gchar *title_utf8 = get_window_title(d->display, clientList[i]);
        qDebug() << title_utf8;
        g_free(title_utf8);
    }

    g_free(clientList);
}

Window * XWindowSwitcher::Extension::getClientList(Display *display, unsigned long *size) const {
    Window *clientList;

    if ((clientList = (Window *)get_property(display, DefaultRootWindow(display), 
                    XA_WINDOW, "_NET_CLIENT_LIST", size)) == NULL) {
        if ((clientList = (Window *)get_property(display, DefaultRootWindow(display), 
                        XA_CARDINAL, "_WIN_CLIENT_LIST", size)) == NULL) {
            qDebug() << "Cannot get client list properties";
            return NULL;
        }
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
        qDebug() << "Cannot get " << prop_name << " property";
        return NULL;
    }
  
    if(xa_ret_type != xa_prop_type) {
        qDebug() << "Invalid type of " << prop_name << " property";
        XFree(ret_prop);
        return NULL;
    }

    // null terminate the result to make string handling easier 
    tmp_size = (ret_format / 8) * ret_nitems;
    /* Correct 64 Architecture implementation of 32 bit data */
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