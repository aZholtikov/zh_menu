# ESP32 ESP-IDF component for hierarchical menu

## Origin

This component is a derivative work based on the original project [menu_manager](https://github.com/MarcioBulla/menu_manager) by Marcio Bulla,
which is distributed under the Apache License, Version 2.0. The original code has been adapted, modified and extended for use with ESP-IDF as the `zh_menu` component.

## Features

1. Multi-level hierarchical menu with support for nested submenus up to a configurable depth limit.
2. Abstract navigation commands (`ZH_MENU_ENTER`, `ZH_MENU_SHOW`, `ZH_MENU_UP`, `ZH_MENU_DOWN`, `ZH_MENU_SELECT`, `ZH_MENU_BACK`) independent of any input device.
3. Configurable callbacks for menu enter, menu exit and current position change.
4. Support for selectable items that can enter a dedicated adjustment mode with repeated `ZH_MENU_SELECT` handling.
5. Support for simple action items that trigger a one-shot callback on selection.
6. Built-in navigation stack that allows returning to the previous menu level with `ZH_MENU_BACK`.
7. Optional cyclic navigation mode for scrolling through menu items in a loop.
8. Per-item selectable flag that separates stateful items from plain action items.
9. Minimal memory footprint with a single dynamically allocated handle and no per-node heap usage.

## Using

In an existing project, run the following command to install the component:

```bash
cd ../your_project/components
git clone https://github.com/aZholtikov/zh_menu
```

In the application, add the component:

```c
#include "zh_menu.h"
```

## Example

```c
#include "zh_menu.h"
#include "zh_encoder.h" // https://github.com/aZholtikov/zh_encoder

#define ENCODER_NUMBER 0x01

zh_encoder_handle_t *encoder_handle = NULL;
zh_menu_handle_t *menu_handle = NULL;

void zh_encoder_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);

void zh_menu_enter_main_menu_callback(void *arg);
void zh_menu_exit_main_menu_callback(void *arg);
void zh_menu_change_menu_callback(zh_menu_node_t *current_node, uint8_t current_index);
void zh_menu_selectable_item_callback(void *arg);
void zh_menu_simple_item_callback(void *args);
void zh_menu_back_command_callback(void *arg);

zh_menu_node_t main_menu = {
    .label = "Main menu",
    .submenu = (zh_menu_node_t[]){
        {.label = "Submenu 1", .submenu = (zh_menu_node_t[]){
                                   // After select this item (ZH_MENU_SELECT) `zh_menu_selectable_item_callback` will called. All commands except `ZH_MENU_BACK` and 'ZH_MENU_SELECT' will inactive.
                                   // For example, in this mode you can use an encoder to change the value of this menu item. You can send anytime 'ZH_MENU_SELECT' for call `zh_menu_selectable_item_callback` again (optional).
                                   // Command `ZH_MENU_BACK` will call `zh_menu_change_menu_callback' and activates the other commands.
                                   {.label = "Submenu 1 Sel 1", .item_cb = &zh_menu_selectable_item_callback, .selectable = true},
                                   // After select this item (ZH_MENU_SELECT) `zh_menu_simple_item_callback` will called.
                                   {.label = "Submenu 1 Item 2", .item_cb = &zh_menu_simple_item_callback},
                                   {.label = "Submenu 1 Item 3", .item_cb = &zh_menu_simple_item_callback},
                                   // After select this item (ZH_MENU_SELECT) `zh_menu_back_command_callback` will called for send 'ZH_MENU_BACK' command.
                                   {.label = "Back", .item_cb = &zh_menu_back_command_callback},
                               },
         .item_numbers = 4},
        {.label = "Submenu 2", .submenu = (zh_menu_node_t[]){
                                   // After select this item (ZH_MENU_SELECT) all commands except `ZH_MENU_BACK` and 'ZH_MENU_SELECT' will inactive.
                                   // Command `ZH_MENU_BACK` will call `zh_menu_change_menu_callback' and activates the other commands.
                                   {.label = "Submenu 2 Sel 1", .selectable = true},
                                   {.label = "Submenu 2 Item 2", .item_cb = &zh_menu_simple_item_callback},
                                   {.label = "Back", .item_cb = &zh_menu_back_command_callback},
                               },
         .item_numbers = 3},
        {.label = "Submenu 3", .submenu = (zh_menu_node_t[]){
                                   {.label = "Subsubmenu 3", .submenu = (zh_menu_node_t[]){
                                                                 {.label = "Subsubmenu 3 Sel 1", .item_cb = &zh_menu_selectable_item_callback, .selectable = true},
                                                                 {.label = "Subsubmenu 3 Item 2", .item_cb = &zh_menu_simple_item_callback},
                                                                 {.label = "Subsubmenu 3 Item 3", .item_cb = &zh_menu_simple_item_callback},
                                                                 {.label = "Back", .item_cb = &zh_menu_back_command_callback},
                                                             },
                                    .item_numbers = 4},
                                   {.label = "Back", .item_cb = &zh_menu_back_command_callback},
                               },
         .item_numbers = 2},
        // After select this item (ZH_MENU_SELECT) `zh_menu_back_command_callback` will called for send 'ZH_MENU_BACK' command.
        // After 'ZH_MENU_BACK' command menu will inactive mode. Current menu position will reset and `zh_menu_exit_main_menu_callback` will called.
        // For activate again send 'ZH_MENU_ENTER' command.
        {.label = "Exit", .item_cb = &zh_menu_back_command_callback},
    },
    .item_numbers = 4,
};

void app_main(void)
{
    esp_log_level_set("zh_menu", ESP_LOG_ERROR);
    esp_log_level_set("zh_encoder", ESP_LOG_ERROR);
    esp_event_loop_create_default();
    esp_event_handler_instance_register(ZH_ENCODER, ESP_EVENT_ANY_ID, &zh_encoder_event_handler, NULL, NULL);
    zh_encoder_init_config_t encoder_config = ZH_ENCODER_INIT_CONFIG_DEFAULT();
    encoder_config.task_priority = 5;
    encoder_config.stack_size = configMINIMAL_STACK_SIZE;
    encoder_config.queue_size = 5;
    encoder_config.a_gpio_number = GPIO_NUM_4;
    encoder_config.b_gpio_number = GPIO_NUM_16;
    encoder_config.s_gpio_number = GPIO_NUM_15;
    encoder_config.encoder_min_value = -1000000;
    encoder_config.encoder_max_value = 1000000;
    encoder_config.encoder_step = 1;
    encoder_config.encoder_number = ENCODER_NUMBER;
    zh_encoder_init(&encoder_config, &encoder_handle);
    zh_menu_init_config_t menu_config = {.root = &main_menu,
                                         .loop = true,
                                         .enter_cb = &zh_menu_enter_main_menu_callback, // Optional.
                                         .exit_cb = &zh_menu_exit_main_menu_callback,   // Optional.
                                         .change_cb = &zh_menu_change_menu_callback};
    zh_menu_init(&menu_config, &menu_handle);
}

void zh_encoder_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    zh_menu_status_t menu_status = {0};
    zh_menu_get(&menu_handle, &menu_status);
    switch (event_id)
    {
    case ZH_BUTTON_EVENT:
        zh_encoder_button_event_on_isr_t *button_event = event_data;
        if (button_event->button_status == true)
        {
            menu_status.is_active == false ? zh_menu_set(&menu_handle, ZH_MENU_ENTER, NULL) : menu_status.is_selected == false ? zh_menu_set(&menu_handle, ZH_MENU_SELECT, NULL)
                                                                                                                               : zh_menu_set(&menu_handle, ZH_MENU_BACK, NULL);
        }
        break;
    case ZH_ENCODER_EVENT:
        zh_encoder_event_on_isr_t *encoder_event = event_data;
        if (menu_status.is_active == true)
        {
            if (menu_status.is_selected == true)
            {
                zh_menu_set(&menu_handle, ZH_MENU_SELECT, encoder_event);
            }
            else
            {
                encoder_event->encoder_status == true ? zh_menu_set(&menu_handle, ZH_MENU_UP, NULL) : zh_menu_set(&menu_handle, ZH_MENU_DOWN, NULL);
            }
        }
    default:
        break;
    }
}

void zh_menu_enter_main_menu_callback(void *arg)
{
    (void)arg;
    printf("Main menu activated.\n");
}

void zh_menu_exit_main_menu_callback(void *arg)
{
    (void)arg;
    printf("Main menu deactivated.\n");
}

void zh_menu_change_menu_callback(zh_menu_node_t *current_node, uint8_t current_index)
{
    printf("\n========== MENU ==========\n");
    printf("Menu: %s\n", current_node->label);
    printf("--------------------------\n");
    for (uint8_t i = 0; i < current_node->item_numbers; ++i)
    {
        printf("%c %s\n", i == current_index ? '>' : ' ', current_node->submenu[i].label);
    }
    printf("==========================\n\n");
    fflush(stdout);
}

void zh_menu_selectable_item_callback(void *arg)
{
    printf("Selectable item callback performed.\n");
    if (arg != NULL)
    {
        zh_encoder_event_on_isr_t *encoder_event = arg;
        printf("Encoder number %d position %0.0f was %s.\n", encoder_event->encoder_number, encoder_event->encoder_position, (encoder_event->encoder_status == true) ? "increased" : "reduced");
    }
}

void zh_menu_simple_item_callback(void *arg)
{
    (void)arg;
    printf("Simple item callback performed.\n");
}

void zh_menu_back_command_callback(void *arg)
{
    (void)arg;
    zh_menu_set(&menu_handle, ZH_MENU_BACK, NULL);
}
```
