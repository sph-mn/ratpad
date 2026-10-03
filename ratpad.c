#include <gtk/gtk.h>
#include <gio/gio.h>
#include <string.h>

#define ratpad_window_width 900
#define ratpad_window_height 700
#define ratpad_tab_spacing 2
#define ratpad_indent_text "  "
#define ratpad_font_family "Sans"
#define ratpad_font_weight 100
#define ratpad_font_size 22
#define ratpad_text_length_auto -1
#define ratpad_zoom_initial 100
#define ratpad_zoom_step 10
#define ratpad_zoom_minimum 50
#define ratpad_zoom_maximum 300
#define ratpad_reload_cancel 0
#define ratpad_reload_confirm 1
#define ratpad_document_data "ratpad-document"
#define ratpad_window_data "ratpad-window"
#define ratpad_notebook_group "ratpad"

struct ratpad_application
{
  GtkApplication* application;
  GtkCssProvider* style_provider;
  int zoom_percent;
};

struct ratpad_window
{
  GtkNotebook* notebook;
  GtkWidget* find_bar;
  GtkEntry* find_entry;
};

struct ratpad_open_context
{
  GtkWindow* window;
  struct ratpad_application* application;
};

struct ratpad_document
{
  GFile* file;
  GtkNotebook* notebook;
  GtkWidget* page;
  GtkTextView* view;
  GtkTextBuffer* buffer;
  GtkWidget* title;
  GtkWidget* modified_indicator;
};

void ratpad_style_update(struct ratpad_application* application)
{
  char* style_text;

  style_text = g_strdup_printf(
    ".ratpad-view { font-family: \"%s\"; font-weight: %d; font-size: %.2fpt; }",
    ratpad_font_family,
    ratpad_font_weight,
    ratpad_font_size * application->zoom_percent / 100.0
  );
  gtk_css_provider_load_from_string(application->style_provider, style_text);
  g_free(style_text);
}

void ratpad_document_free(gpointer data)
{
  struct ratpad_document* document;

  document = data;
  g_clear_object(&document->file);
  g_free(document);
}

void ratpad_notebook_title_update(GtkNotebook* notebook)
{
  GtkWidget* page;
  struct ratpad_document* document;
  char* title;

  page = gtk_notebook_get_nth_page(notebook, gtk_notebook_get_current_page(notebook));
  document = g_object_get_data(G_OBJECT(page), ratpad_document_data);
  if (gtk_text_buffer_get_modified(document->buffer)) {
    title = g_strdup_printf("%s ●", gtk_label_get_text(GTK_LABEL(document->title)));
  } else {
    title = g_strdup(gtk_label_get_text(GTK_LABEL(document->title)));
  }
  gtk_window_set_title(GTK_WINDOW(gtk_widget_get_root(GTK_WIDGET(notebook))), title);
  g_free(title);
}

void ratpad_document_title_update(struct ratpad_document* document)
{
  char* basename;

  if (document->file) {
    basename = g_file_get_basename(document->file);
    gtk_label_set_text(GTK_LABEL(document->title), basename);
    g_free(basename);
  } else {
    gtk_label_set_text(GTK_LABEL(document->title), "new");
  }
  if (gtk_notebook_get_nth_page(document->notebook, gtk_notebook_get_current_page(document->notebook)) == document->page) {
    ratpad_notebook_title_update(document->notebook);
  }
}

void ratpad_on_modified_changed(GtkTextBuffer* buffer, gpointer user_data)
{
  struct ratpad_document* document;

  document = user_data;
  gtk_widget_set_visible(document->modified_indicator, gtk_text_buffer_get_modified(buffer));
  if (gtk_notebook_get_nth_page(document->notebook, gtk_notebook_get_current_page(document->notebook)) == document->page) {
    ratpad_notebook_title_update(document->notebook);
  }
}

void ratpad_document_load(struct ratpad_document* document, GFile* file)
{
  GFile* referenced_file;
  char* text;

  referenced_file = g_object_ref(file);
  text = 0;
  g_file_load_contents(file, 0, &text, 0, 0, 0);
  gtk_text_buffer_set_text(document->buffer, text, ratpad_text_length_auto);
  g_clear_object(&document->file);
  document->file = referenced_file;
  gtk_text_buffer_set_modified(document->buffer, FALSE);
  ratpad_document_title_update(document);
  g_free(text);
}

void ratpad_document_save(struct ratpad_document* document, GFile* file)
{
  GFile* referenced_file;
  GtkTextIter start_iter;
  GtkTextIter end_iter;
  char* text;

  gtk_text_buffer_get_bounds(document->buffer, &start_iter, &end_iter);
  text = gtk_text_buffer_get_text(document->buffer, &start_iter, &end_iter, FALSE);
  g_file_replace_contents(file, text, strlen(text), 0, FALSE, G_FILE_CREATE_NONE, 0, 0, 0);
  referenced_file = g_object_ref(file);
  g_clear_object(&document->file);
  document->file = referenced_file;
  gtk_text_buffer_set_modified(document->buffer, FALSE);
  ratpad_document_title_update(document);
  g_free(text);
}

gboolean ratpad_on_zoom_scroll(
  GtkEventControllerScroll* controller,
  double unused_horizontal_delta,
  double vertical_delta,
  gpointer user_data
)
{
  struct ratpad_application* application;
  GdkModifierType state;

  (void)unused_horizontal_delta;
  application = user_data;
  state = gtk_event_controller_get_current_event_state(GTK_EVENT_CONTROLLER(controller));
  if (state & GDK_CONTROL_MASK) {
    if (vertical_delta < 0 && application->zoom_percent < ratpad_zoom_maximum) {
      application->zoom_percent = application->zoom_percent + ratpad_zoom_step;
      ratpad_style_update(application);
    } else if (vertical_delta > 0 && application->zoom_percent > ratpad_zoom_minimum) {
      application->zoom_percent = application->zoom_percent - ratpad_zoom_step;
      ratpad_style_update(application);
    }
    return TRUE;
  }
  return FALSE;
}

struct ratpad_document* ratpad_document_create(GtkNotebook* notebook, struct ratpad_application* application)
{
  struct ratpad_document* document;
  GtkWidget* tab_box;
  GtkWidget* view;
  GtkEventController* scroll_controller;
  int page_number;

  document = g_new0(struct ratpad_document, 1);
  document->notebook = notebook;
  document->page = gtk_scrolled_window_new();
  view = gtk_text_view_new();
  document->view = GTK_TEXT_VIEW(view);
  document->buffer = gtk_text_view_get_buffer(document->view);
  document->title = gtk_label_new("new");
  document->modified_indicator = gtk_label_new("●");
  gtk_widget_set_visible(document->modified_indicator, FALSE);
  gtk_text_view_set_accepts_tab(document->view, FALSE);
  gtk_text_buffer_set_enable_undo(document->buffer, TRUE);
  gtk_widget_add_css_class(view, "ratpad-view");
  gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(document->page), view);

  scroll_controller = gtk_event_controller_scroll_new(GTK_EVENT_CONTROLLER_SCROLL_VERTICAL | GTK_EVENT_CONTROLLER_SCROLL_DISCRETE);
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(scroll_controller), GTK_PHASE_CAPTURE);
  g_signal_connect(scroll_controller, "scroll", G_CALLBACK(ratpad_on_zoom_scroll), application);
  gtk_widget_add_controller(view, scroll_controller);

  tab_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, ratpad_tab_spacing);
  gtk_box_append(GTK_BOX(tab_box), document->title);
  gtk_box_append(GTK_BOX(tab_box), document->modified_indicator);
  g_object_set_data_full(G_OBJECT(document->page), ratpad_document_data, document, ratpad_document_free);
  page_number = gtk_notebook_append_page(notebook, document->page, tab_box);
  gtk_notebook_set_current_page(notebook, page_number);
  g_signal_connect(document->buffer, "modified-changed", G_CALLBACK(ratpad_on_modified_changed), document);
  ratpad_notebook_title_update(notebook);
  gtk_widget_grab_focus(view);
  return document;
}

struct ratpad_document* ratpad_document_open(GtkNotebook* notebook, struct ratpad_application* application, GFile* file)
{
  struct ratpad_document* document;

  if (gtk_notebook_get_n_pages(notebook) == 1) {
    document = g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(notebook, 0)), ratpad_document_data);
    if (!document->file && !gtk_text_buffer_get_char_count(document->buffer)) {
      ratpad_document_load(document, file);
      gtk_widget_grab_focus(GTK_WIDGET(document->view));
      return document;
    }
  }
  document = ratpad_document_create(notebook, application);
  ratpad_document_load(document, file);
  return document;
}

void ratpad_on_save_ready(GObject* source, GAsyncResult* result, gpointer user_data)
{
  GtkFileDialog* dialog;
  GFile* file;
  struct ratpad_document* document;

  document = user_data;
  dialog = GTK_FILE_DIALOG(source);
  file = gtk_file_dialog_save_finish(dialog, result, 0);
  if (file) {
    ratpad_document_save(document, file);
    g_object_unref(file);
  }
  g_object_unref(document->page);
}

void ratpad_document_save_request(struct ratpad_document* document, GtkWindow* window)
{
  GtkFileDialog* dialog;

  if (document->file) {
    ratpad_document_save(document, document->file);
    return;
  }
  dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Save file");
  gtk_file_dialog_set_modal(dialog, TRUE);
  g_object_ref(document->page);
  gtk_file_dialog_save(dialog, window, 0, ratpad_on_save_ready, document);
  g_object_unref(dialog);
}

void ratpad_on_reload_choice(GObject* source, GAsyncResult* result, gpointer user_data)
{
  GtkAlertDialog* dialog;
  struct ratpad_document* document;
  int choice;

  document = user_data;
  dialog = GTK_ALERT_DIALOG(source);
  choice = gtk_alert_dialog_choose_finish(dialog, result, 0);
  if (choice == ratpad_reload_confirm) {
    ratpad_document_load(document, document->file);
  }
  g_object_unref(document->page);
}

void ratpad_document_reload_request(struct ratpad_document* document, GtkWindow* window)
{
  GtkAlertDialog* dialog;
  char* buttons[] = {"Cancel", "Reload", 0};

  if (document->file) {
    if (gtk_text_buffer_get_modified(document->buffer)) {
      dialog = gtk_alert_dialog_new("Reload file?");
      gtk_alert_dialog_set_detail(dialog, "Unsaved changes will be discarded.");
      gtk_alert_dialog_set_modal(dialog, TRUE);
      gtk_alert_dialog_set_buttons(dialog, (void*)buttons);
      gtk_alert_dialog_set_cancel_button(dialog, ratpad_reload_cancel);
      gtk_alert_dialog_set_default_button(dialog, ratpad_reload_confirm);
      g_object_ref(document->page);
      gtk_alert_dialog_choose(dialog, window, 0, ratpad_on_reload_choice, document);
      g_object_unref(dialog);
    } else {
      ratpad_document_load(document, document->file);
    }
  }
}

void ratpad_on_open_ready(GObject* source, GAsyncResult* result, gpointer user_data)
{
  struct ratpad_open_context* context;
  GtkFileDialog* dialog;
  GFile* file;

  context = user_data;
  dialog = GTK_FILE_DIALOG(source);
  file = gtk_file_dialog_open_finish(dialog, result, 0);
  if (file) {
    ratpad_document_open(((struct ratpad_window*)g_object_get_data(G_OBJECT(context->window), ratpad_window_data))->notebook, context->application, file);
    g_object_unref(file);
  }
  g_object_unref(context->window);
  g_free(context);
}

void ratpad_open_request(GtkWindow* window, struct ratpad_application* application)
{
  struct ratpad_open_context* context;
  GtkFileDialog* dialog;

  context = g_new0(struct ratpad_open_context, 1);
  context->window = g_object_ref(window);
  context->application = application;
  dialog = gtk_file_dialog_new();
  gtk_file_dialog_set_title(dialog, "Open file");
  gtk_file_dialog_set_modal(dialog, TRUE);
  gtk_file_dialog_open(dialog, window, 0, ratpad_on_open_ready, context);
  g_object_unref(dialog);
}

void ratpad_find_hide(struct ratpad_window* window_state)
{
  GtkWidget* page;
  struct ratpad_document* document;

  gtk_widget_set_visible(window_state->find_bar, FALSE);
  page = gtk_notebook_get_nth_page(window_state->notebook, gtk_notebook_get_current_page(window_state->notebook));
  if (page) {
    document = g_object_get_data(G_OBJECT(page), ratpad_document_data);
    gtk_widget_grab_focus(GTK_WIDGET(document->view));
  }
}

void ratpad_on_find_next(gpointer unused_widget, gpointer user_data)
{
  struct ratpad_window* window_state;
  GtkWidget* page;
  struct ratpad_document* document;
  GtkTextIter start_iter;
  GtkTextIter match_start;
  GtkTextIter match_end;
  char* search_text;
  gboolean found;

  (void)unused_widget;
  window_state = user_data;
  search_text = g_strdup(gtk_editable_get_text(GTK_EDITABLE(window_state->find_entry)));
  if (!search_text[0]) {
    g_free(search_text);
    return;
  }
  page = gtk_notebook_get_nth_page(window_state->notebook, gtk_notebook_get_current_page(window_state->notebook));
  document = g_object_get_data(G_OBJECT(page), ratpad_document_data);
  if (!gtk_text_buffer_get_selection_bounds(document->buffer, &match_start, &start_iter)) {
    gtk_text_buffer_get_iter_at_mark(document->buffer, &start_iter, gtk_text_buffer_get_insert(document->buffer));
  }
  found = gtk_text_iter_forward_search(
    &start_iter,
    search_text,
    GTK_TEXT_SEARCH_CASE_INSENSITIVE,
    &match_start,
    &match_end,
    0
  );
  if (!found) {
    gtk_text_buffer_get_start_iter(document->buffer, &start_iter);
    found = gtk_text_iter_forward_search(
      &start_iter,
      search_text,
      GTK_TEXT_SEARCH_CASE_INSENSITIVE,
      &match_start,
      &match_end,
      0
    );
  }
  if (found) {
    gtk_text_buffer_select_range(document->buffer, &match_start, &match_end);
    gtk_text_view_scroll_to_iter(document->view, &match_start, 0.1, FALSE, 0.0, 0.0);
  }
  g_free(search_text);
}

void ratpad_on_find_close(GtkButton* unused_button, gpointer user_data)
{
  (void)unused_button;
  ratpad_find_hide(user_data);
}

void ratpad_on_page_added(GtkNotebook* notebook, GtkWidget* child, guint unused_page_number, gpointer unused_data)
{
  struct ratpad_document* document;
  GtkNotebookPage* page;

  (void)unused_page_number;
  (void)unused_data;
  document = g_object_get_data(G_OBJECT(child), ratpad_document_data);
  document->notebook = notebook;
  page = gtk_notebook_get_page(notebook, child);
  g_object_set(page, "tab-expand", TRUE, (void*)0);
  gtk_notebook_set_tab_reorderable(notebook, child, TRUE);
  gtk_notebook_set_tab_detachable(notebook, child, TRUE);
  gtk_notebook_set_show_tabs(notebook, gtk_notebook_get_n_pages(notebook) > 1);
  ratpad_notebook_title_update(notebook);
}

void ratpad_on_page_removed(GtkNotebook* notebook, GtkWidget* unused_child, guint unused_page_number, gpointer unused_data)
{
  GtkRoot* root;

  (void)unused_child;
  (void)unused_page_number;
  (void)unused_data;
  if (gtk_notebook_get_n_pages(notebook)) {
    gtk_notebook_set_show_tabs(notebook, gtk_notebook_get_n_pages(notebook) > 1);
    ratpad_notebook_title_update(notebook);
    return;
  }
  root = gtk_widget_get_root(GTK_WIDGET(notebook));
  if (root) {
    gtk_window_destroy(GTK_WINDOW(root));
  }
}

void ratpad_on_switch_page(GtkNotebook* notebook, GtkWidget* unused_page, guint unused_page_number, gpointer unused_data)
{
  (void)unused_page;
  (void)unused_page_number;
  (void)unused_data;
  ratpad_notebook_title_update(notebook);
}

GtkWindow* ratpad_window_create(struct ratpad_application* application);

GtkNotebook* ratpad_on_create_window(GtkNotebook* unused_notebook, GtkWidget* unused_page, gpointer user_data)
{
  struct ratpad_application* application;
  struct ratpad_window* window_state;
  GtkWindow* window;

  (void)unused_notebook;
  (void)unused_page;
  application = user_data;
  window = ratpad_window_create(application);
  window_state = g_object_get_data(G_OBJECT(window), ratpad_window_data);
  return window_state->notebook;
}

gboolean ratpad_on_key_pressed(
  GtkEventControllerKey* controller,
  guint keyval,
  guint unused_keycode,
  GdkModifierType state,
  gpointer user_data
)
{
  struct ratpad_application* application;
  struct ratpad_window* window_state;
  GtkWindow* window;
  GtkNotebook* notebook;
  GtkWidget* page;
  GtkWidget* focus;
  struct ratpad_document* document;
  GdkModifierType modifiers;

  (void)unused_keycode;
  application = user_data;
  window = GTK_WINDOW(gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(controller)));
  window_state = g_object_get_data(G_OBJECT(window), ratpad_window_data);
  notebook = window_state->notebook;
  modifiers = state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_ALT_MASK | GDK_SUPER_MASK);

  if (!modifiers && keyval == GDK_KEY_Escape && gtk_widget_get_visible(window_state->find_bar)) {
    ratpad_find_hide(window_state);
    return TRUE;
  }
  if (modifiers == GDK_CONTROL_MASK && keyval == GDK_KEY_q) {
    g_application_quit(G_APPLICATION(gtk_window_get_application(window)));
    return TRUE;
  }
  if (modifiers == GDK_CONTROL_MASK && keyval == GDK_KEY_n) {
    ratpad_document_create(notebook, application);
    return TRUE;
  }
  if (modifiers == GDK_CONTROL_MASK && keyval == GDK_KEY_o) {
    ratpad_open_request(window, application);
    return TRUE;
  }
  if (modifiers == GDK_CONTROL_MASK && keyval == GDK_KEY_f) {
    gtk_widget_set_visible(window_state->find_bar, TRUE);
    gtk_widget_grab_focus(GTK_WIDGET(window_state->find_entry));
    return TRUE;
  }
  if (modifiers == GDK_CONTROL_MASK && keyval == GDK_KEY_Page_Up) {
    gtk_notebook_prev_page(notebook);
    return TRUE;
  }
  if (modifiers == GDK_CONTROL_MASK && keyval == GDK_KEY_Page_Down) {
    gtk_notebook_next_page(notebook);
    return TRUE;
  }

  page = gtk_notebook_get_nth_page(notebook, gtk_notebook_get_current_page(notebook));
  if (!page) {
    return FALSE;
  }
  document = g_object_get_data(G_OBJECT(page), ratpad_document_data);

  if (modifiers == GDK_CONTROL_MASK && keyval == GDK_KEY_s) {
    ratpad_document_save_request(document, window);
    return TRUE;
  }
  if (modifiers == GDK_CONTROL_MASK && keyval == GDK_KEY_r) {
    ratpad_document_reload_request(document, window);
    return TRUE;
  }
  if (modifiers == GDK_CONTROL_MASK && keyval == GDK_KEY_w) {
    gtk_notebook_remove_page(notebook, gtk_notebook_get_current_page(notebook));
    return TRUE;
  }
  if (!modifiers && keyval == GDK_KEY_Tab) {
    focus = gtk_window_get_focus(window);
    if (focus == GTK_WIDGET(document->view)) {
      gtk_text_buffer_begin_user_action(document->buffer);
      gtk_text_buffer_delete_selection(document->buffer, TRUE, TRUE);
      gtk_text_buffer_insert_at_cursor(document->buffer, ratpad_indent_text, ratpad_text_length_auto);
      gtk_text_buffer_end_user_action(document->buffer);
      return TRUE;
    }
  }
  return FALSE;
}

GtkWindow* ratpad_window_create(struct ratpad_application* application)
{
  struct ratpad_window* window_state;
  GtkWidget* window;
  GtkWidget* root_box;
  GtkWidget* notebook;
  GtkWidget* find_bar;
  GtkWidget* close_button;
  GtkWidget* find_entry;
  GtkWidget* next_button;
  GtkEventController* key_controller;

  window_state = g_new0(struct ratpad_window, 1);
  window = gtk_application_window_new(application->application);
  root_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  notebook = gtk_notebook_new();
  find_bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
  close_button = gtk_button_new_with_label("close");
  find_entry = gtk_entry_new();
  next_button = gtk_button_new_with_label("next");

  window_state->notebook = GTK_NOTEBOOK(notebook);
  window_state->find_bar = find_bar;
  window_state->find_entry = GTK_ENTRY(find_entry);
  g_object_set_data_full(G_OBJECT(window), ratpad_window_data, window_state, g_free);

  gtk_window_set_title(GTK_WINDOW(window), "new");
  gtk_window_set_default_size(GTK_WINDOW(window), ratpad_window_width, ratpad_window_height);
  gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook), TRUE);
  gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
  gtk_notebook_set_group_name(GTK_NOTEBOOK(notebook), ratpad_notebook_group);
  gtk_widget_set_vexpand(notebook, TRUE);
  gtk_widget_set_hexpand(find_entry, TRUE);
  gtk_widget_set_margin_top(find_bar, 6);
  gtk_widget_set_margin_bottom(find_bar, 6);
  gtk_widget_set_margin_start(find_bar, 6);
  gtk_widget_set_margin_end(find_bar, 6);
  gtk_box_append(GTK_BOX(find_bar), close_button);
  gtk_box_append(GTK_BOX(find_bar), find_entry);
  gtk_box_append(GTK_BOX(find_bar), next_button);
  gtk_widget_set_visible(find_bar, FALSE);
  gtk_box_append(GTK_BOX(root_box), notebook);
  gtk_box_append(GTK_BOX(root_box), find_bar);
  gtk_window_set_child(GTK_WINDOW(window), root_box);

  g_signal_connect(close_button, "clicked", G_CALLBACK(ratpad_on_find_close), window_state);
  g_signal_connect(find_entry, "activate", G_CALLBACK(ratpad_on_find_next), window_state);
  g_signal_connect(next_button, "clicked", G_CALLBACK(ratpad_on_find_next), window_state);

  key_controller = gtk_event_controller_key_new();
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(key_controller), GTK_PHASE_CAPTURE);
  g_signal_connect(key_controller, "key-pressed", G_CALLBACK(ratpad_on_key_pressed), application);
  gtk_widget_add_controller(window, key_controller);

  g_signal_connect(notebook, "page-added", G_CALLBACK(ratpad_on_page_added), 0);
  g_signal_connect(notebook, "page-removed", G_CALLBACK(ratpad_on_page_removed), 0);
  g_signal_connect(notebook, "switch-page", G_CALLBACK(ratpad_on_switch_page), 0);
  g_signal_connect(notebook, "create-window", G_CALLBACK(ratpad_on_create_window), application);
  gtk_window_present(GTK_WINDOW(window));
  return GTK_WINDOW(window);
}

void ratpad_on_application_activate(GtkApplication* unused_gtk_application, gpointer user_data)
{
  struct ratpad_application* application;
  GtkWindow* window;

  (void)unused_gtk_application;
  application = user_data;
  window = ratpad_window_create(application);
  ratpad_document_create(((struct ratpad_window*)g_object_get_data(G_OBJECT(window), ratpad_window_data))->notebook, application);
}

void ratpad_on_application_open(GApplication* unused_gtk_application, GFile** files, int file_count, char* unused_hint, gpointer user_data)
{
  struct ratpad_application* application;
  struct ratpad_window* window_state;
  GtkWindow* window;
  GtkNotebook* notebook;
  GList* windows;
  int index;

  (void)unused_gtk_application;
  (void)unused_hint;
  application = user_data;
  window = gtk_application_get_active_window(application->application);
  if (window) {
    window_state = g_object_get_data(G_OBJECT(window), ratpad_window_data);
    notebook = window_state->notebook;
  } else {
    windows = gtk_application_get_windows(application->application);
    if (windows) {
      window = windows->data;
    } else {
      window = ratpad_window_create(application);
    }
    window_state = g_object_get_data(G_OBJECT(window), ratpad_window_data);
    notebook = window_state->notebook;
  }
  index = 0;
  while (index < file_count) {
    ratpad_document_open(notebook, application, files[index]);
    index = index + 1;
  }
  gtk_window_present(window);
}

void ratpad_on_application_startup(GApplication* unused_application, gpointer user_data)
{
  struct ratpad_application* application;
  GdkDisplay* display;

  (void)unused_application;
  application = user_data;
  application->style_provider = gtk_css_provider_new();
  g_object_set(gtk_settings_get_default(), "gtk-interface-color-scheme", GTK_INTERFACE_COLOR_SCHEME_DARK, (void*)0);
  ratpad_style_update(application);
  display = gdk_display_get_default();
  gtk_style_context_add_provider_for_display(
    display,
    GTK_STYLE_PROVIDER(application->style_provider),
    GTK_STYLE_PROVIDER_PRIORITY_APPLICATION
  );
}

int main(int argument_count, char** argument_vector)
{
  struct ratpad_application application;
  int return_code;

  application.application = gtk_application_new("local.ratpad", G_APPLICATION_HANDLES_OPEN);
  application.style_provider = 0;
  application.zoom_percent = ratpad_zoom_initial;
  g_signal_connect(application.application, "startup", G_CALLBACK(ratpad_on_application_startup), &application);
  g_signal_connect(application.application, "activate", G_CALLBACK(ratpad_on_application_activate), &application);
  g_signal_connect(application.application, "open", G_CALLBACK(ratpad_on_application_open), &application);
  return_code = g_application_run(G_APPLICATION(application.application), argument_count, argument_vector);
  g_clear_object(&application.style_provider);
  g_clear_object(&application.application);
  return return_code;
}
