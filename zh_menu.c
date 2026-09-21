/*
 * This file is part of the zh_menu component, which is a derivative work
 * based on the original project "menu_manager" by Marcio Bulla, available at:
 * https://github.com/MarcioBulla/menu_manager
 * The original project is distributed under the Apache License, Version 2.0.
 * The original code was adapted, modified and extended for use with ESP-IDF
 * as the zh_menu component.
 */

#include "zh_menu.h"

static const char *TAG = "zh_menu";

#define ZH_LOGI(msg, ...) ESP_LOGI(TAG, msg, ##__VA_ARGS__)
#define ZH_LOGE(msg, err, ...) ESP_LOGE(TAG, "[%s:%d:%s] " msg, __FILE__, __LINE__, esp_err_to_name(err), ##__VA_ARGS__)

#define ZH_ERROR_CHECK(cond, err, cleanup, msg, ...) \
    if (!(cond))                                     \
    {                                                \
        ZH_LOGE(msg, err, ##__VA_ARGS__);            \
        cleanup;                                     \
        return err;                                  \
    }

#define ZH_MENU_MAX_DEPTH 8 /*!< Maximum depth of nested submenus. Limits the navigation stack size */

/**
 * @brief Internal representation of the menu handle.
 *
 * Stores the stack of traversed levels, the current and root nodes,
 * user callbacks, state flags and the current index of the highlighted
 * item.
 *
 * @note The menu_stack is used to return to the previous level on the ZH_MENU_BACK command.
 *
 * @warning When ZH_MENU_MAX_DEPTH is reached, further descent is not performed to avoid stack overflow.
 */
struct _zh_menu_handle_t
{
    zh_menu_node_t *menu_stack[ZH_MENU_MAX_DEPTH];                          /*!< Stack of traversed menu levels */
    zh_menu_node_t *current_menu;                                           /*!< Current active menu level */
    zh_menu_node_t *root;                                                   /*!< Root node of the menu tree */
    void (*change_cb)(zh_menu_node_t *current_path, uint8_t current_index); /*!< Callback for the current position change */
    void (*enter_cb)(void *args);                                           /*!< Callback for entering the menu */
    void (*exit_cb)(void *args);                                            /*!< Callback for exiting the menu */
    bool loop;                                                              /*!< Cyclic navigation flag */
    uint8_t menu_depth;                                                     /*!< Current nesting depth */
    uint8_t current_index;                                                  /*!< Index of the highlighted item on the current level */
    bool is_activated;                                                      /*!< Menu activity flag */
    bool is_selected;                                                       /*!< Selection state flag of the current item */
};

esp_err_t zh_menu_init(const zh_menu_init_config_t *config, zh_menu_handle_t **handle)
{
    ZH_LOGI("Menu initialization started.");
    ZH_ERROR_CHECK(config != NULL && config->root != NULL && handle != NULL && config->root->submenu != NULL && config->root->item_numbers != 0, ESP_ERR_INVALID_ARG, NULL, "Menu initialization failed. Invalid argument.");
    ZH_ERROR_CHECK(*handle == NULL, ESP_ERR_INVALID_STATE, NULL, "Menu initialization failed. Menu is already initialized.");
    *handle = heap_caps_calloc(1, sizeof(zh_menu_handle_t), MALLOC_CAP_8BIT);
    ZH_ERROR_CHECK(*handle != NULL, ESP_ERR_NO_MEM, NULL, "Menu initialization failed. Failed to allocate menu handle.");
    (*handle)->change_cb = config->change_cb;
    (*handle)->enter_cb = config->enter_cb;
    (*handle)->exit_cb = config->exit_cb;
    (*handle)->current_menu = config->root;
    (*handle)->root = config->root;
    (*handle)->loop = config->loop;
    ZH_LOGI("Menu initialization completed successfully.");
    return ESP_OK;
}

esp_err_t zh_menu_deinit(zh_menu_handle_t **handle)
{
    ZH_LOGI("Menu deinitialization started.");
    ZH_ERROR_CHECK(handle != NULL && *handle != NULL, ESP_ERR_INVALID_ARG, NULL, "Menu deinitialization failed. Invalid argument.");
    heap_caps_free(*handle);
    *handle = NULL;
    ZH_LOGI("Menu deinitialization completed successfully.");
    return ESP_OK;
}

esp_err_t zh_menu_set(zh_menu_handle_t **handle, zh_menu_navigate_t command, void *arg)
{
    ZH_LOGI("Menu set position started.");
    ZH_ERROR_CHECK(handle != NULL && *handle != NULL && command >= ZH_MENU_ENTER && command < ZH_MENU_MAX, ESP_ERR_INVALID_ARG, NULL, "Menu set position failed. Invalid argument.");
    switch (command)
    {
    case ZH_MENU_ENTER:
        if ((*handle)->is_activated == false)
        {
            (*handle)->is_activated = true;
            (*handle)->is_selected = false;
            (*handle)->current_menu = (*handle)->root;
            (*handle)->current_index = 0;
            (*handle)->menu_depth = 0;
            memset((*handle)->menu_stack, 0, sizeof((*handle)->menu_stack));
            if ((*handle)->enter_cb != NULL)
            {
                (*handle)->enter_cb(arg);
            }
            if ((*handle)->change_cb != NULL)
            {
                (*handle)->change_cb((*handle)->current_menu, (*handle)->current_index);
            }
        }
        break;
    case ZH_MENU_SHOW:
        if ((*handle)->is_activated == true)
        {
            if ((*handle)->change_cb != NULL)
            {
                (*handle)->change_cb((*handle)->current_menu, (*handle)->current_index);
            }
        }
        break;
    case ZH_MENU_UP:
        if ((*handle)->current_menu == NULL || (*handle)->current_menu->item_numbers == 0)
        {
            break;
        }
        if ((*handle)->is_activated == true && (*handle)->is_selected == false)
        {
            if ((*handle)->current_index > 0)
            {
                --(*handle)->current_index;
            }
            else if ((*handle)->loop == true)
            {
                (*handle)->current_index = (*handle)->current_menu->item_numbers - 1;
            }
            if ((*handle)->change_cb != NULL)
            {
                (*handle)->change_cb((*handle)->current_menu, (*handle)->current_index);
            }
        }
        break;
    case ZH_MENU_DOWN:
        if ((*handle)->current_menu == NULL || (*handle)->current_menu->item_numbers == 0)
        {
            break;
        }
        if ((*handle)->is_activated == true && (*handle)->is_selected == false)
        {
            if ((*handle)->current_index < (*handle)->current_menu->item_numbers - 1)
            {
                ++(*handle)->current_index;
            }
            else if ((*handle)->loop == true)
            {
                (*handle)->current_index = 0;
            }
            if ((*handle)->change_cb != NULL)
            {
                (*handle)->change_cb((*handle)->current_menu, (*handle)->current_index);
            }
        }
        break;
    case ZH_MENU_SELECT:
        if ((*handle)->is_activated == true)
        {
            if ((*handle)->current_menu->submenu == NULL || (*handle)->current_index >= (*handle)->current_menu->item_numbers)
            {
                break;
            }
            if ((*handle)->is_selected == true)
            {
                if ((*handle)->current_menu->submenu[(*handle)->current_index].item_cb != NULL)
                {
                    (*handle)->current_menu->submenu[(*handle)->current_index].item_cb(arg);
                }
            }
            else if ((*handle)->current_menu->submenu[(*handle)->current_index].selectable == true)
            {
                (*handle)->is_selected = true;
                if ((*handle)->current_menu->submenu[(*handle)->current_index].item_cb != NULL)
                {
                    (*handle)->current_menu->submenu[(*handle)->current_index].item_cb(arg);
                }
            }
            else if ((*handle)->current_menu->submenu[(*handle)->current_index].item_cb != NULL)
            {
                (*handle)->current_menu->submenu[(*handle)->current_index].item_cb(arg);
            }
            else if ((*handle)->current_menu->submenu[(*handle)->current_index].submenu != NULL && (*handle)->current_menu->submenu[(*handle)->current_index].item_numbers > 0)
            {
                if ((*handle)->menu_depth >= ZH_MENU_MAX_DEPTH)
                {
                    break;
                }
                (*handle)->menu_stack[(*handle)->menu_depth] = (*handle)->current_menu;
                ++(*handle)->menu_depth;
                (*handle)->current_menu = &(*handle)->current_menu->submenu[(*handle)->current_index];
                (*handle)->current_index = 0;
                if ((*handle)->change_cb != NULL)
                {
                    (*handle)->change_cb((*handle)->current_menu, (*handle)->current_index);
                }
            }
        }
        break;
    case ZH_MENU_BACK:
        if ((*handle)->is_activated == true)
        {
            if ((*handle)->is_selected == true)
            {
                (*handle)->is_selected = false;
                if ((*handle)->change_cb != NULL)
                {
                    (*handle)->change_cb((*handle)->current_menu, (*handle)->current_index);
                }
                break;
            }
            if ((*handle)->menu_depth > 0)
            {
                --(*handle)->menu_depth;
                (*handle)->current_menu = (*handle)->menu_stack[(*handle)->menu_depth];
                (*handle)->menu_stack[(*handle)->menu_depth] = NULL;
                (*handle)->current_index = 0;
                if ((*handle)->change_cb != NULL)
                {
                    (*handle)->change_cb((*handle)->current_menu, (*handle)->current_index);
                }
            }
            else
            {
                (*handle)->is_activated = false;
                (*handle)->is_selected = false;
                (*handle)->current_menu = (*handle)->root;
                (*handle)->current_index = 0;
                (*handle)->menu_depth = 0;
                memset((*handle)->menu_stack, 0, sizeof((*handle)->menu_stack));
                if ((*handle)->exit_cb != NULL)
                {
                    (*handle)->exit_cb(arg);
                }
            }
        }
        break;
    default:
        break;
    }
    ZH_LOGI("Menu set position completed successfully.");
    return ESP_OK;
}

esp_err_t zh_menu_get(zh_menu_handle_t **handle, zh_menu_status_t *status)
{
    ZH_LOGI("Menu get status started.");
    ZH_ERROR_CHECK(handle != NULL && *handle != NULL && status != NULL, ESP_ERR_INVALID_ARG, NULL, "Menu get status failed. Invalid argument.");
    status->is_active = (*handle)->is_activated;
    status->is_selected = (*handle)->is_selected;
    ZH_LOGI("Menu get status completed successfully.");
    return ESP_OK;
}