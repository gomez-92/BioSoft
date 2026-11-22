// Ver ui_components.h.

#include "ui_components.h"
#include "ui_theme.h"

lv_obj_t* ui_box_create(lv_obj_t* parent) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return box;
}

lv_obj_t* ui_card_create(lv_obj_t* parent) {
    lv_obj_t* card = ui_box_create(parent);
    lv_obj_add_style(card, &st_card, 0);
    return card;
}

lv_obj_t* ui_data_card_create(lv_obj_t* parent, int32_t width, int32_t height, const char* text,
                              lv_obj_t** values) {
    lv_obj_t* card = ui_card_create(parent);
    lv_obj_set_size(card, width, height);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(card, 3, 0);

    // Todo plano, sin una caja intermedia para valor + unidad: con una caja
    // de tamano LV_SIZE_CONTENT alineada a la derecha, su ancho quedaba el
    // del texto inicial y al llegar el valor real desbordaba hacia la
    // izquierda (en la placa se veia la unidad y un pedazo del ultimo
    // digito). La etiqueta crece y empuja los valores contra el borde.
    lv_obj_t* label = ui_label_create(card, &st_lbl, text);
    lv_obj_set_flex_grow(label, 1);
    if (values) *values = card;
    return card;
}

lv_obj_t* ui_data_card_add_value(lv_obj_t* values, const char* value, const char* unit) {
    lv_obj_t* label = ui_label_create(values, &st_val16, value);
    ui_label_create(values, &st_lbl, unit);
    return label;
}

lv_obj_t* ui_header_create(lv_obj_t* parent, const char* text, lv_obj_t** title) {
    lv_obj_t* header = ui_box_create(parent);
    lv_obj_add_style(header, &st_header, 0);
    lv_obj_set_pos(header, 0, 0);

    // Sin estilo propio: hereda color y fuente del encabezado, asi cambia
    // solo cuando EN CURSO le pone st_hdr_warn.
    lv_obj_t* label = ui_label_create(header, NULL, text);
    lv_obj_set_flex_grow(label, 1);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    if (title) *title = label;
    return header;
}

static lv_obj_t* status_icon_create(lv_obj_t* parent, const char* symbol) {
    lv_obj_t* icon = ui_box_create(parent);
    lv_obj_set_size(icon, 20, 20);
    lv_obj_t* label = ui_label_create(icon, NULL, symbol);
    lv_obj_center(label);
    return icon;
}

void ui_header_add_status_icons(lv_obj_t* header, ui_status_icons_t* out) {
    lv_obj_t* box = ui_box_create(header);
    lv_obj_set_size(box, 92, 20);
    lv_obj_set_flex_flow(box, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(box, 4, 0);

    out->mega   = status_icon_create(box, LV_SYMBOL_USB);
    out->sd     = status_icon_create(box, LV_SYMBOL_SD_CARD);
    out->wifi   = status_icon_create(box, LV_SYMBOL_WIFI);
    out->broker = status_icon_create(box, LV_SYMBOL_UPLOAD);
}

void ui_status_icon_set(lv_obj_t* icon, bool ok) {
    if (icon == NULL) return;
    // lv_obj_add_style no evita duplicados: se saca siempre antes.
    lv_obj_remove_style(icon, &st_icon_fail, 0);
    if (!ok) lv_obj_add_style(icon, &st_icon_fail, 0);
}

void ui_button_set_kind(lv_obj_t* button, ui_button_kind_t kind) {
    lv_obj_remove_style(button, &st_btn_pri, 0);
    lv_obj_remove_style(button, &st_btn_pri_pr, LV_STATE_PRESSED);
    lv_obj_remove_style(button, &st_btn_sec, 0);
    lv_obj_remove_style(button, &st_btn_sec_pr, LV_STATE_PRESSED);
    lv_obj_remove_style(button, &st_btn_stop, 0);
    lv_obj_remove_style(button, &st_btn_stop_pr, LV_STATE_PRESSED);
    lv_obj_remove_style(button, &st_btn_dis, LV_STATE_DISABLED);

    switch (kind) {
        case UI_BTN_PRIMARY:
            lv_obj_add_style(button, &st_btn_pri, 0);
            lv_obj_add_style(button, &st_btn_pri_pr, LV_STATE_PRESSED);
            break;
        case UI_BTN_SECONDARY:
            lv_obj_add_style(button, &st_btn_sec, 0);
            lv_obj_add_style(button, &st_btn_sec_pr, LV_STATE_PRESSED);
            break;
        case UI_BTN_STOP:
            lv_obj_add_style(button, &st_btn_stop, 0);
            lv_obj_add_style(button, &st_btn_stop_pr, LV_STATE_PRESSED);
            break;
    }
    // Ultimo: con el estado DISABLED tiene que ganarle al estilo del tipo.
    lv_obj_add_style(button, &st_btn_dis, LV_STATE_DISABLED);
}

lv_obj_t* ui_button_create(lv_obj_t* parent, ui_button_kind_t kind, const char* text, lv_obj_t** label) {
    lv_obj_t* button = lv_button_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_remove_flag(button, LV_OBJ_FLAG_SCROLLABLE);
    ui_button_set_kind(button, kind);

    lv_obj_t* text_label = ui_label_create(button, NULL, text);
    lv_obj_center(text_label);
    if (label) *label = text_label;
    return button;
}
