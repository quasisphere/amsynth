//
//  amsynth_dssi_gui.c
//  amsynth_dssi_gui
//
//  Created by Nick Dowell on 04/06/2011.
//  Copyright 2011 __MyCompanyName__. All rights reserved.
//

#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <lo/lo.h>

#include "controls.h"
#include "Preset.h"
#include "GUI/editor_pane.h"

////////////////////////////////////////////////////////////////////////////////

#define MAX_PATH 160

static GtkWindow *_window = NULL;
static GtkAdjustment *_adjustments[kAmsynthParameterCount] = {0};
static gboolean _dont_send_control_changes = FALSE;

static char *_osc_path = NULL;
lo_server _osc_server = NULL;
lo_address _osc_host_addr = NULL;

////////////////////////////////////////////////////////////////////////////////

//
// convenience function that allocates a formatted string
// the returned string is only valid until the next call to the function
// so be sure to copy the result if you need to use it beyond that!
// not at all thread safe, and probably a bad idea...!
//
char *tmpstr(const char *format, ...)
{
    static char *string = NULL;
    
    if (string) {
        free(string);
        string = NULL;
    }
    
    va_list args;
    va_start(args, format);
    vasprintf(&string, format, args);
    va_end(args);
    
    return string;
}

void osc_error(int num, const char *msg, const char *path)
{
    abort();
}

////////////////////////////////////////////////////////////////////////////////
//
// handle message sent by plugin host
//

int osc_control_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
    assert(types[0] == 'i');
    assert(types[1] == 'f');
    float value = argv[1]->f;
    int port_number = argv[0]->i;
    int parameter_index = port_number - 2;
    printf("OSC: control %2d = %f\n", port_number, value);
    g_assert(parameter_index < kAmsynthParameterCount);
    _dont_send_control_changes = TRUE;
    gtk_adjustment_set_value(_adjustments[parameter_index], value);
    _dont_send_control_changes = FALSE;
    return 0;
}

int osc_samplerate_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
    assert(types[0] == 'i');
    printf("OSC: sample rate = %d\n", argv[0]->i);
    return 0;
}

int osc_program_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
    assert(types[0] == 'i');
    assert(types[1] == 'i');
    printf("OSC: selected bank %d program %2d\n", argv[0]->i, argv[1]->i);
    return 0;
}

int osc_show_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
    printf("OSC: show GUI window\n");
    gtk_window_present(_window);
    return 0;
}

int osc_hide_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
    printf("OSC: hide GUI window\n");
    gtk_widget_hide(GTK_WIDGET(_window));
    return 0;
}

int osc_quit_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
    printf("OSC: quit GUI process\n");
    gtk_main_quit();
    return 0;
}

int osc_fallback_handler(const char *path, const char *types, lo_arg **argv, int argc, void *data, void *user_data)
{
    fprintf(stderr, "unhandled OSC message (path = '%s' types = '%s')\n", path, types);
    return 1;
}

////////////////////////////////////////////////////////////////////////////////
//
// Messages sent to the plugin host
//

int host_request_update()
{
    char value[MAX_PATH] = "";
    char *url = lo_server_get_url(_osc_server);
    sprintf(value, "%s%s", url, _osc_path);
    int err = lo_send(_osc_host_addr, tmpstr("%s/update", _osc_path), "s", value);
    free(url);
    return err;
}

int host_set_control(int control, float value)
{
    fprintf(stderr, "host_set_control(%d, %f)\n", control, value);
    int err = lo_send(_osc_host_addr, tmpstr("%s/control", _osc_path), "if", control, value);
    return err;
}

int host_set_program(int bank, int program)
{
    int err = lo_send(_osc_host_addr, tmpstr("%s/program", _osc_path), "ii", bank, program);
    return err;
}

int host_gui_exiting()
{
    int err = lo_send(_osc_host_addr, tmpstr("%s/exiting", _osc_path), "");
    return err;
}

////////////////////////////////////////////////////////////////////////////////

void on_window_deleted()
{
    _window = NULL;
    host_gui_exiting();
    gtk_main_quit();
}

void on_adjustment_value_changed(GtkAdjustment *adjustment, gpointer user_data)
{
    if (_dont_send_control_changes)
        return;
    size_t parameter_index = (size_t)user_data;
    g_assert(parameter_index < kAmsynthParameterCount);
    int port_number = parameter_index + 2;
    host_set_control(port_number, gtk_adjustment_get_value(adjustment));
}

////////////////////////////////////////////////////////////////////////////////

int main(int argc, char *argv[])
{
    if (argc < 5) {
        g_critical("not enough arguments supplied");
        return 1;
    }

    gtk_init(&argc, &argv);

    //char *exe_path = argv[0];
    char *host_url = argv[1];
    //char *lib_name = argv[2];
    char *plug_name = argv[3];
    char *identifier = argv[4];
    
    _osc_path = lo_url_get_path(host_url);
    _osc_host_addr = lo_address_new_from_url(host_url);
    
    _osc_server = lo_server_new(NULL, osc_error);
    lo_server_add_method(_osc_server, tmpstr("/%s/control",     _osc_path), "if", osc_control_handler,     NULL);
    lo_server_add_method(_osc_server, tmpstr("/%s/sample-rate", _osc_path), "i",  osc_samplerate_handler,  NULL);
    lo_server_add_method(_osc_server, tmpstr("/%s/program",     _osc_path), "ii", osc_program_handler,     NULL);
    lo_server_add_method(_osc_server, tmpstr("/%s/show",        _osc_path), NULL, osc_show_handler,        NULL);
    lo_server_add_method(_osc_server, tmpstr("/%s/hide",        _osc_path), NULL, osc_hide_handler,        NULL);
    lo_server_add_method(_osc_server, tmpstr("/%s/quit",        _osc_path), NULL, osc_quit_handler,        NULL);
    lo_server_add_method(_osc_server, NULL, NULL, osc_fallback_handler, NULL);
    
    host_request_update();
    
    gdk_input_add(lo_server_get_socket_fd(_osc_server), GDK_INPUT_READ, (GdkInputFunction)lo_server_recv_noblock, _osc_server);
    
    _window = GTK_WINDOW(gtk_window_new(GTK_WINDOW_TOPLEVEL));

    gtk_window_set_title(_window, tmpstr("%s - %s", plug_name, identifier));
	gtk_signal_connect(GTK_OBJECT(_window), "delete-event", on_window_deleted, NULL);

    g_setenv("AMSYNTH_DATA_DIR", g_build_filename(INSTALL_PREFIX, "share", "amSynth", NULL), FALSE);

    size_t i; for (i=0; i<kAmsynthParameterCount; i++) {
        gdouble value = 0, lower = 0, upper = 0, step_increment = 0;
        get_parameter_properties(i, &lower, &upper, &value, &step_increment);
        _adjustments[i] = (GtkAdjustment *)gtk_adjustment_new(value, lower, upper, step_increment, 0, 0);
        g_signal_connect(_adjustments[i], "value-changed", (GCallback)&on_adjustment_value_changed, (gpointer)i);
    }

    GtkWidget *editor = editor_pane_new(_adjustments);
    gtk_container_add(GTK_CONTAINER(_window), editor);
    gtk_widget_show_all(GTK_WIDGET(editor));
    
    gtk_main();
    
    return 0;
}

void modal_midi_learn(int param_index)
{
}

////////////////////////////////////////////////////////////////////////////////

