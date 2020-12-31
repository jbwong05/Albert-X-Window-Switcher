#pragma once
#include <QObject>
#include <memory>
#include "albert/extension.h"
#include "albert/queryhandler.h"
#include "albert/util/standardactions.h"

// X11 headers must be included before Qt headers in cpp file
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>

// Need to undef Bool because Qt headers redefine it
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
            char * get_property(Display *disp, Window win, Atom xa_prop_type, char *prop_name, unsigned long *size) const;
            char * get_window_title(Display *disp, Window win) const;
    };

    struct ActivateWindowAction : public Core::StandardActionBase {
        public:
            ActivateWindowAction(const QString &text, Display *display, Window window);
            void activate() override;

        private:
            Display *display;
            Window window;
            
            void client_msg(char *msg);
    };
}
