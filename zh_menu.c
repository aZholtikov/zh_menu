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

#define ZH_ERROR_CHECK_CONT(cond, cleanup, msg, ...) \
    if (!(cond))                                     \
    {                                                \
        ZH_LOGE(msg, ESP_FAIL, ##__VA_ARGS__);       \
        cleanup;                                     \
    }

#define ZH_MENU_MAX_DEPTH 8
#define ZH_MENU_ARG_MAX_SIZE 128

/**
 * @brief Internal representation of the menu handle.
 *
 * Stores the stack of traversed levels, the current and root nodes,
 * user callbacks, state flags and the current index of the highlighted
 * item.
 *
 * @note The menu_stack is used to return to the previous level on the ZH_MENU_BACK command.
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
    uint8_t viewport_rows;                                                  /*!< Number of visible rows (0 = show all) */
    uint8_t scroll_offset;                                                  /*!< Index of the first visible row */
    bool is_activated;                                                      /*!< Menu activity flag */
    bool is_selected;                                                       /*!< Selection state flag of the current item */
    portMUX_TYPE lock;                                                      /*!< Spinlock protecting fields accessed from ISR and task context */
};

/**
 * @brief Internal queue item for passing navigation commands.
 *
 * Stores the command, associated handle, argument size and a copy of
 * the user argument data. Used to transfer commands from ISR/task context
 * to the background processing task.
 */
typedef struct
{
    zh_menu_navigate_t command;        /*!< Navigation command to process */
    zh_menu_handle_t *handle;          /*!< Menu handle associated with the command */
    uint32_t arg_size;                 /*!< Size of the argument data in bytes */
    uint8_t arg[ZH_MENU_ARG_MAX_SIZE]; /*!< Argument data buffer passed to user callbacks */
} zh_menu_queue_t;

TaskHandle_t zh_menu = NULL;
static QueueHandle_t _queue_handle = NULL;

static zh_menu_stats_t _stats = {0};
volatile static uint8_t _menu_counter = 0;

/**
 * @brief Validate the initialization configuration.
 *
 * Checks task priority, stack size, queue size and root node parameters.
 * Copies user callbacks and initial menu state into the handle.
 *
 * @param config Pointer to the initialization configuration (must not be NULL)
 * @param handle Pointer to the menu handle being initialized (must not be NULL)
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if any configuration parameter is out of range or root is invalid
 */
static esp_err_t _zh_menu_validate_config(const zh_menu_init_config_t *config, zh_menu_handle_t *handle);

/**
 * @brief Create the internal command queue if it does not exist.
 *
 * The queue is created only once, on the first initialization.
 * Subsequent calls return success without recreating the queue.
 *
 * @param config Pointer to the initialization configuration (must not be NULL)
 *
 * @return ESP_OK on success
 * @return ESP_FAIL if queue creation fails
 */
static esp_err_t _zh_menu_resources_init(const zh_menu_init_config_t *config);

/**
 * @brief Create the processing task if it does not exist.
 *
 * The task is created only once, on the first initialization.
 * Subsequent calls return success without recreating the task.
 *
 * @param config Pointer to the initialization configuration (must not be NULL)
 *
 * @return ESP_OK on success
 * @return ESP_FAIL if task creation fails
 */
static esp_err_t _zh_menu_task_init(const zh_menu_init_config_t *config);

/**
 * @brief Delete the processing task and queue on rollback.
 *
 * Called when initialization fails after task and queue have been created.
 * Cleans up resources to avoid leaks.
 */
static void _zh_menu_rollback_task_and_resources(void);

/**
 * @brief Recalculate scroll_offset so that current_index stays in the window.
 *
 * Called whenever current_index changes, so the selected row is always visible.
 * The window "sticks" to the edges only when necessary.
 *
 * @param handle Pointer to the menu handle (lock must be held by the caller)
 */
static void _zh_menu_adjust_scroll(zh_menu_handle_t *handle);

/**
 * @brief Background task that processes menu navigation commands from the queue.
 *
 * Receives commands and executes state transitions, invoking user callbacks
 * for position changes, menu entry, exit and item selection.
 *
 * @param pvParameter Task parameter (unused).
 */
static void _zh_menu_isr_processing_task(void *pvParameter);

esp_err_t zh_menu_init(const zh_menu_init_config_t *config, zh_menu_handle_t **handle)
{
    ZH_LOGI("Menu initialization started.");
    ZH_ERROR_CHECK(config != NULL && handle != NULL, ESP_ERR_INVALID_ARG, NULL, "Menu initialization failed. Invalid argument.");
    ZH_ERROR_CHECK(*handle == NULL, ESP_ERR_INVALID_STATE, NULL, "Menu initialization failed. Menu is already initialized.");
    *handle = heap_caps_calloc(1, sizeof(zh_menu_handle_t), MALLOC_CAP_8BIT);
    ZH_ERROR_CHECK(*handle != NULL, ESP_ERR_NO_MEM, NULL, "Menu initialization failed. Failed to allocate menu handle.");
    ZH_ERROR_CHECK(_zh_menu_validate_config(config, *handle) == ESP_OK, ESP_FAIL, heap_caps_free(*handle); *handle = NULL, "Menu initialization failed. Initial configuration check failed.");
    ZH_ERROR_CHECK(_zh_menu_resources_init(config) == ESP_OK, ESP_FAIL, heap_caps_free(*handle); *handle = NULL, "Menu initialization failed. Resources initialization failed.");
    ZH_ERROR_CHECK(_zh_menu_task_init(config) == ESP_OK, ESP_FAIL, _zh_menu_rollback_task_and_resources();
                   heap_caps_free(*handle); *handle = NULL, "Menu initialization failed. Processing task initialization failed.");
    ++_menu_counter;
    portMUX_INITIALIZE(&(*handle)->lock);
    if (_stats.min_stack_size == 0)
    {
        _stats.min_stack_size = config->stack_size;
    }
    ZH_LOGI("Menu initialization completed successfully.");
    return ESP_OK;
}

esp_err_t zh_menu_deinit(zh_menu_handle_t **handle)
{
    ZH_LOGI("Menu deinitialization started.");
    ZH_ERROR_CHECK(handle != NULL && *handle != NULL, ESP_ERR_INVALID_ARG, NULL, "Menu deinitialization failed. Invalid argument.");
    if (--_menu_counter == 0)
    {
        if (zh_menu != NULL)
        {
            vTaskDelete(zh_menu);
            zh_menu = NULL;
        }
        if (_queue_handle != NULL)
        {
            vQueueDelete(_queue_handle);
            _queue_handle = NULL;
        }
    }
    heap_caps_free(*handle);
    *handle = NULL;
    ZH_LOGI("Menu deinitialization completed successfully.");
    return ESP_OK;
}

esp_err_t zh_menu_set(zh_menu_handle_t **handle, zh_menu_navigate_t command, const void *arg, uint8_t arg_size)
{
    ZH_LOGI("Menu set position started.");
    ZH_ERROR_CHECK(handle != NULL && *handle != NULL && command >= ZH_MENU_ENTER && command < ZH_MENU_MAX, ESP_ERR_INVALID_ARG, NULL, "Menu set position failed. Invalid argument.");
    ZH_ERROR_CHECK(arg_size <= ZH_MENU_ARG_MAX_SIZE, ESP_ERR_INVALID_ARG, NULL, "Menu set position failed. Invalid argument.");
    ZH_ERROR_CHECK(arg_size == 0 || arg != NULL, ESP_ERR_INVALID_ARG, NULL, "Menu set position failed. Invalid argument.");
    taskENTER_CRITICAL(&(*handle)->lock);
    zh_menu_queue_t menu_queue = {.handle = *handle,
                                  .command = command,
                                  .arg_size = arg_size};
    if (arg_size > 0)
    {
        memcpy(menu_queue.arg, arg, arg_size);
    }
    taskEXIT_CRITICAL(&(*handle)->lock);
    ZH_ERROR_CHECK_CONT(xQueueSend(_queue_handle, &menu_queue, pdMS_TO_TICKS(10)) == pdTRUE, ++_stats.queue_overflow_error, "Menu isr processing failed. Queue overflow.");
    ZH_LOGI("Menu set position completed successfully.");
    return ESP_OK;
}

esp_err_t zh_menu_get(zh_menu_handle_t **handle, zh_menu_status_t *status)
{
    ZH_LOGI("Menu get status started.");
    ZH_ERROR_CHECK(handle != NULL && *handle != NULL && status != NULL, ESP_ERR_INVALID_ARG, NULL, "Menu get status failed. Invalid argument.");
    taskENTER_CRITICAL(&(*handle)->lock);
    status->is_active = (*handle)->is_activated;
    status->is_selected = (*handle)->is_selected;
    taskEXIT_CRITICAL(&(*handle)->lock);
    ZH_LOGI("Menu get status completed successfully.");
    return ESP_OK;
}

esp_err_t zh_menu_get_view(zh_menu_handle_t **handle, zh_menu_view_t *view)
{
    ZH_LOGI("Menu get view started.");
    ZH_ERROR_CHECK(handle != NULL && *handle != NULL && view != NULL, ESP_ERR_INVALID_ARG, NULL, "Menu get view failed. Invalid argument.");
    taskENTER_CRITICAL(&(*handle)->lock);
    if ((*handle)->current_menu == NULL)
    {
        view->first_visible = 0;
        view->visible_count = 0;
    }
    else if ((*handle)->viewport_rows == 0 ||
             (*handle)->current_menu->item_numbers <= (*handle)->viewport_rows)
    {
        view->first_visible = 0;
        view->visible_count = (*handle)->current_menu->item_numbers;
    }
    else
    {
        view->first_visible = (*handle)->scroll_offset;
        uint8_t rest = (*handle)->current_menu->item_numbers - (*handle)->scroll_offset;
        view->visible_count = (rest > (*handle)->viewport_rows) ? (*handle)->viewport_rows : rest;
    }
    taskEXIT_CRITICAL(&(*handle)->lock);
    ZH_LOGI("Menu get view completed successfully.");
    return ESP_OK;
}

const zh_menu_stats_t *zh_menu_get_stats(void)
{
    return &_stats;
}

void zh_menu_reset_stats(void)
{
    ZH_LOGI("Error statistic reset started.");
    _stats.queue_overflow_error = 0;
    _stats.min_stack_size = 0;
    ZH_LOGI("Error statistic reset successfully.");
}

static esp_err_t _zh_menu_validate_config(const zh_menu_init_config_t *config, zh_menu_handle_t *handle)
{
    ZH_ERROR_CHECK(config->task_priority >= 1 && config->stack_size >= configMINIMAL_STACK_SIZE, ESP_ERR_INVALID_ARG, NULL, "Invalid task settings.");
    ZH_ERROR_CHECK(config->queue_size >= 1, ESP_ERR_INVALID_ARG, NULL, "Invalid queue size.");
    ZH_ERROR_CHECK(config->root != NULL && config->root->submenu != NULL && config->root->item_numbers > 0, ESP_ERR_INVALID_ARG, NULL, "Invalid argument.");
    handle->change_cb = config->change_cb;
    handle->enter_cb = config->enter_cb;
    handle->exit_cb = config->exit_cb;
    handle->current_menu = config->root;
    handle->root = config->root;
    handle->loop = config->loop;
    handle->viewport_rows = config->viewport_rows;
    handle->scroll_offset = 0;
    return ESP_OK;
}

static esp_err_t _zh_menu_resources_init(const zh_menu_init_config_t *config)
{
    if (_menu_counter == 0)
    {
        _queue_handle = xQueueCreate(config->queue_size, sizeof(zh_menu_queue_t));
        ZH_ERROR_CHECK(_queue_handle != NULL, ESP_FAIL, NULL, "Failed to create queue.");
    }
    return ESP_OK;
}

static esp_err_t _zh_menu_task_init(const zh_menu_init_config_t *config)
{
    if (_menu_counter == 0)
    {
        ZH_ERROR_CHECK(xTaskCreatePinnedToCore(&_zh_menu_isr_processing_task, "zh_menu_isr_processing", config->stack_size, NULL, config->task_priority, &zh_menu, tskNO_AFFINITY) == pdPASS,
                       ESP_FAIL, NULL, "Failed to create isr processing task.");
    }
    return ESP_OK;
}

static void _zh_menu_rollback_task_and_resources(void)
{
    if (_menu_counter == 0)
    {
        if (zh_menu != NULL)
        {
            vTaskDelete(zh_menu);
            zh_menu = NULL;
        }
        if (_queue_handle != NULL)
        {
            vQueueDelete(_queue_handle);
            _queue_handle = NULL;
        }
    }
}

static void _zh_menu_adjust_scroll(zh_menu_handle_t *handle)
{
    if (handle->current_menu == NULL || handle->viewport_rows == 0)
    {
        handle->scroll_offset = 0;
        return;
    }
    uint8_t total = handle->current_menu->item_numbers;
    if (total <= handle->viewport_rows)
    {
        handle->scroll_offset = 0;
        return;
    }
    if (handle->current_index < handle->scroll_offset)
    {
        handle->scroll_offset = handle->current_index;
    }
    else if (handle->current_index >= (uint8_t)(handle->scroll_offset + handle->viewport_rows))
    {
        handle->scroll_offset = handle->current_index - handle->viewport_rows + 1;
    }
    if (handle->scroll_offset > (uint8_t)(total - handle->viewport_rows))
    {
        handle->scroll_offset = total - handle->viewport_rows;
    }
}

static void _zh_menu_isr_processing_task(void *pvParameter)
{
    (void)pvParameter;
    zh_menu_queue_t menu_queue = {0};
    while (xQueueReceive(_queue_handle, &menu_queue, portMAX_DELAY) == pdTRUE)
    {
        zh_menu_handle_t *handle = menu_queue.handle;
        void (*enter_cb)(void *) = NULL;
        void (*exit_cb)(void *) = NULL;
        void (*item_cb)(void *) = NULL;
        void (*change_cb)(zh_menu_node_t *, uint8_t) = NULL;
        zh_menu_node_t *change_node = NULL;
        uint8_t change_index = 0;
        void *cb_arg = (menu_queue.arg_size > 0) ? (void *)menu_queue.arg : NULL;
        taskENTER_CRITICAL(&handle->lock);
        switch (menu_queue.command)
        {
        case ZH_MENU_ENTER:
            if (handle->is_activated == false)
            {
                handle->is_activated = true;
                handle->is_selected = false;
                handle->current_menu = handle->root;
                handle->current_index = 0;
                handle->scroll_offset = 0;
                handle->menu_depth = 0;
                memset(handle->menu_stack, 0, sizeof(handle->menu_stack));
                enter_cb = handle->enter_cb;
                if (handle->change_cb != NULL)
                {
                    change_cb = handle->change_cb;
                    change_node = handle->current_menu;
                    change_index = handle->current_index;
                }
            }
            break;
        case ZH_MENU_SHOW:
            if (handle->is_activated == true && handle->change_cb != NULL)
            {
                change_cb = handle->change_cb;
                change_node = handle->current_menu;
                change_index = handle->current_index;
            }
            break;
        case ZH_MENU_UP:
            if (handle->current_menu == NULL || handle->current_menu->item_numbers == 0)
            {
                break;
            }
            if (handle->is_activated == true && handle->is_selected == false)
            {
                if (handle->current_index > 0)
                {
                    --handle->current_index;
                }
                else if (handle->loop == true)
                {
                    handle->current_index = handle->current_menu->item_numbers - 1;
                }
                _zh_menu_adjust_scroll(handle);
                if (handle->change_cb != NULL)
                {
                    change_cb = handle->change_cb;
                    change_node = handle->current_menu;
                    change_index = handle->current_index;
                }
            }
            break;
        case ZH_MENU_DOWN:
            if (handle->current_menu == NULL || handle->current_menu->item_numbers == 0)
            {
                break;
            }
            if (handle->is_activated == true && handle->is_selected == false)
            {
                if (handle->current_index < handle->current_menu->item_numbers - 1)
                {
                    ++handle->current_index;
                }
                else if (handle->loop == true)
                {
                    handle->current_index = 0;
                }
                _zh_menu_adjust_scroll(handle);
                if (handle->change_cb != NULL)
                {
                    change_cb = handle->change_cb;
                    change_node = handle->current_menu;
                    change_index = handle->current_index;
                }
            }
            break;
        case ZH_MENU_SELECT:
            if (handle->is_activated != true)
            {
                break;
            }
            if (handle->current_menu == NULL || handle->current_menu->submenu == NULL || handle->current_index >= handle->current_menu->item_numbers)
            {
                break;
            }
            {
                zh_menu_node_t *item = &handle->current_menu->submenu[handle->current_index];
                if (handle->is_selected == true)
                {
                    if (item->item_cb != NULL)
                    {
                        item_cb = item->item_cb;
                    }
                }
                else if (item->selectable == true)
                {
                    handle->is_selected = true;
                    if (item->item_cb != NULL)
                    {
                        item_cb = item->item_cb;
                    }
                }
                else if (item->item_cb != NULL)
                {
                    item_cb = item->item_cb;
                }
                else if (item->submenu != NULL && item->item_numbers > 0)
                {
                    if (handle->menu_depth < ZH_MENU_MAX_DEPTH)
                    {
                        handle->menu_stack[handle->menu_depth] = handle->current_menu;
                        ++handle->menu_depth;
                        handle->current_menu = item;
                        handle->current_index = 0;
                        handle->scroll_offset = 0;
                        if (handle->change_cb != NULL)
                        {
                            change_cb = handle->change_cb;
                            change_node = handle->current_menu;
                            change_index = handle->current_index;
                        }
                    }
                }
            }
            break;
        case ZH_MENU_BACK:
            if (handle->is_activated != true)
            {
                break;
            }
            if (handle->is_selected == true)
            {
                handle->is_selected = false;
                if (handle->change_cb != NULL)
                {
                    change_cb = handle->change_cb;
                    change_node = handle->current_menu;
                    change_index = handle->current_index;
                }
            }
            else if (handle->menu_depth > 0)
            {
                --handle->menu_depth;
                handle->current_menu = handle->menu_stack[handle->menu_depth];
                handle->menu_stack[handle->menu_depth] = NULL;
                handle->current_index = 0;
                handle->scroll_offset = 0;
                if (handle->change_cb != NULL)
                {
                    change_cb = handle->change_cb;
                    change_node = handle->current_menu;
                    change_index = handle->current_index;
                }
            }
            else
            {
                handle->is_activated = false;
                handle->is_selected = false;
                handle->current_menu = handle->root;
                handle->current_index = 0;
                handle->scroll_offset = 0;
                handle->menu_depth = 0;
                memset(handle->menu_stack, 0, sizeof(handle->menu_stack));
                exit_cb = handle->exit_cb;
            }
            break;
        default:
            break;
        }
        taskEXIT_CRITICAL(&handle->lock);
        if (enter_cb != NULL)
        {
            enter_cb(cb_arg);
        }
        if (change_cb != NULL)
        {
            change_cb(change_node, change_index);
        }
        if (item_cb != NULL)
        {
            item_cb(cb_arg);
        }
        if (exit_cb != NULL)
        {
            exit_cb(cb_arg);
        }
        _stats.min_stack_size = (uint32_t)uxTaskGetStackHighWaterMark(NULL);
    }
    vTaskDelete(NULL);
}