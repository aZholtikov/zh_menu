/**
 * @file zh_menu.h
 *
 * @brief Hierarchical menu component for ESP-IDF.
 *
 * This header provides types and functions for building a multi-level
 * menu with navigation, selection handling and user callback support.
 * The component is independent of any particular input method (buttons,
 * encoder, interface commands) and operates with abstract navigation
 * commands.
 *
 * This component is a derivative work based on the original project
 * "menu_manager" by Marcio Bulla, available at:
 * https://github.com/MarcioBulla/menu_manager
 * The original project is distributed under the Apache License,
 * Version 2.0. The original code was adapted, modified and extended
 * for use with ESP-IDF as the zh_menu component.
 *
 * Key features:
 * - Support for nested submenus with a depth limit
 * - Configurable callbacks for position change, enter and exit
 * - Cyclic navigation mode over menu items
 * - Flexible handling of selectable and non-selectable items
 * - Low memory footprint due to dynamic allocation of the handle
 *
 * @note The component functions are thread-safe only to the extent
 *       provided by the caller. External synchronization is required
 *       when accessed from multiple tasks.
 */

#pragma once

#include "string.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Opaque menu handle.
     *
     * The internal structure is hidden from the user and is defined
     * in the implementation file. All operations are performed through
     * a pointer to this type.
     */
    typedef struct _zh_menu_handle_t zh_menu_handle_t;

    /**
     * @brief Menu navigation commands.
     */
    typedef enum
    {
        ZH_MENU_ENTER,  /*!< Activate the menu and go to the root level */
        ZH_MENU_SHOW,   /*!< Force refresh of the current state via callback */
        ZH_MENU_UP,     /*!< Move selection to the previous item */
        ZH_MENU_DOWN,   /*!< Move selection to the next item */
        ZH_MENU_SELECT, /*!< Select the current item or enter a submenu */
        ZH_MENU_BACK,   /*!< Return to the previous level or exit the menu */
        ZH_MENU_MAX     /*!< Upper bound of valid commands, not a command itself */
    } zh_menu_navigate_t;

    /**
     * @brief Menu tree node.
     *
     * Describes a single menu item: its displayed label, nested submenu,
     * number of items in the submenu, user callback and selectable flag.
     *
     * @warning The submenu array must exist during the entire lifetime
     *          of the menu. The component does not copy nodes.
     */
    typedef struct zh_menu_node_t
    {
        const char *label;              /*!< Displayed label of the menu item */
        struct zh_menu_node_t *submenu; /*!< Pointer to an array of nested items or NULL */
        uint8_t item_numbers;           /*!< Number of items in the nested submenu array */
        void (*item_cb)(void *args);    /*!< Callback invoked when the item is selected */
        bool selectable;                /*!< Flag indicating that the item can be selected */
    } zh_menu_node_t;

    /**
     * @brief Current menu state.
     *
     * Used to pass activity and selection status to external code
     * through the zh_menu_get() function.
     */
    typedef struct
    {
        bool is_active;   /*!< Flag indicating that the menu is activated */
        bool is_selected; /*!< Flag indicating that the current item is in the selection state */
    } zh_menu_status_t;

    /**
     * @brief Menu initialization configuration.
     *
     * Contains the root node, a set of user callbacks and the cyclic navigation flag.
     *
     * @note The change_cb, enter_cb and exit_cb callbacks may be NULL
     *       if the corresponding handling is not required.
     */
    typedef struct
    {
        zh_menu_node_t *root;                                                   /*!< Root node of the menu tree */
        void (*change_cb)(zh_menu_node_t *current_path, uint8_t current_index); /*!< Callback for the current position change */
        void (*enter_cb)(void *args);                                           /*!< Callback for entering the menu */
        void (*exit_cb)(void *args);                                            /*!< Callback for exiting the menu */
        bool loop;                                                              /*!< Cyclic navigation flag over items */
    } zh_menu_init_config_t;

    /**
     * @brief Initialize the menu.
     *
     * Creates the menu handle, stores the configuration and prepares
     * the internal state. After a successful call, the menu is in
     * an inactive state until the ZH_MENU_ENTER command.
     *
     * @param[in] config Initialization configuration (must not be NULL)
     * @param[out] handle Pointer to the handle pointer (must be NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if config, config->root or handle is NULL
     * @return ESP_ERR_INVALID_STATE if *handle is already initialized
     * @return ESP_ERR_NO_MEM if memory allocation for the handle failed
     */
    esp_err_t zh_menu_init(const zh_menu_init_config_t *config, zh_menu_handle_t **handle);

    /**
     * @brief Deinitialize the menu.
     *
     * Frees the dynamic memory occupied by the handle and resets the pointer to it.
     *
     * @param[in,out] handle Pointer to the handle pointer (must not be NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle or *handle is NULL
     */
    esp_err_t zh_menu_deinit(zh_menu_handle_t **handle);

    /**
     * @brief Execute a menu navigation command.
     *
     * Processes one of the zh_menu_navigate_t commands, changing
     * the internal menu state and invoking the corresponding callbacks.
     * The exact behavior depends on the current activity and selection state.
     *
     * @param[in,out] handle Pointer to the handle pointer (must not be NULL)
     * @param[in] command Navigation command. Must be less than ZH_MENU_MAX
     * @param[in] arg Arbitrary argument passed to the user callbacks (enter_cb, exit_cb, item_cb) (may be NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle, *handle is NULL or command >= ZH_MENU_MAX
     *
     * @note Commands that are invalid in the current state are ignored and do not cause an error.
     */
    esp_err_t zh_menu_set(zh_menu_handle_t **handle, zh_menu_navigate_t command, void *arg);

    /**
     * @brief Get the current menu state.
     *
     * Fills the status structure with the activity and selection flags.
     *
     * @param[in] handle Pointer to the handle pointer (must not be NULL)
     * @param[out] status Pointer to the status structure to be filled (must not be NULL)
     *
     * @return ESP_OK on success
     * @return ESP_ERR_INVALID_ARG if handle, *handle or status is NULL
     */
    esp_err_t zh_menu_get(zh_menu_handle_t **handle, zh_menu_status_t *status);

#ifdef __cplusplus
}
#endif