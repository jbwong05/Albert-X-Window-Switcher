#pragma once
#include <QObject>
#include <memory>
//#include <X11/Xatom.h>
//#include <X11/cursorfont.h>
//#include <X11/Xmu/WinUtil.h>

#include "albert/extension.h"
#include "albert/queryhandler.h"
#include <X11/Xlib.h>
#include <glib.h>

#undef Bool

namespace XWindowSwitcher {

class Private;

    class Extension final : public Core::Extension, public Core::QueryHandler {
        Q_OBJECT
        Q_PLUGIN_METADATA(IID ALBERT_EXTENSION_IID FILE "metadata.json")

        public:

            Extension();
            ~Extension() override;

            QString name() const override { return "Template"; }
            QWidget *widget(QWidget *parent = nullptr) override;
            void setupSession() override;
            void teardownSession() override;
            void handleQuery(Core::Query * query) const override;

        private:

            std::unique_ptr<Private> d;
            Window * getClientList(Display *display, unsigned long *size) const;
            gchar * get_property(Display *disp, Window win, Atom xa_prop_type, gchar *prop_name, unsigned long *size) const;
            gchar * get_window_title(Display *disp, Window win) const;
    };
}
