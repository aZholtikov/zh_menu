# Описание компонента zh_menu

## Общие сведения

**zh_menu** — это компонент для ESP-IDF, реализующий иерархическое многоуровневое меню с поддержкой вложенных подменю, конфигурируемых обратных вызовов и абстрактных команд навигации, независимых от конкретного устройства ввода (кнопок, энкодера, команд интерфейса).

Компонент управляет стеком уровней меню, обрабатывает команды навигации в фоновой задаче FreeRTOS и рассылает состояние через пользовательские колбэки, позволяя внешнему коду рендерить меню на дисплее или в консоли.

## Основные возможности

1. **Вложенные подменю** — поддержка глубины вложенности до 8 уровней (`ZH_MENU_MAX_DEPTH`) с автоматическим стеком навигации.
2. **Абстрактные команды навигации** — `ZH_MENU_ENTER`, `ZH_MENU_SHOW`, `ZH_MENU_UP`, `ZH_MENU_DOWN`, `ZH_MENU_SELECT`, `ZH_MENU_BACK` не привязаны к конкретным устройствам ввода.
3. **Настраиваемые обратные вызовы** — `enter_cb`, `exit_cb` и `change_cb` для реакции на вход в меню, выход из него и изменение текущей позиции.
4. **Выбираемые и простые элементы** — флаг `selectable` разделяет элементы, входящие в режим ручной корректировки, и элементы с одноразовым действием.
5. **Циклическая навигация** — опциональный режим зацикливания при перемещении `UP` / `DOWN` по элементам.
6. **Минимальный расход памяти** — динамически выделяется только один хэндл; узлы меню (`zh_menu_node_t`) не копируются и используют пользовательскую память.
7. **Встроенная статистика** — счётчик переполнений очереди обработки и watermark свободного стека задачи.

## Внутреннее устройство

### Структура `zh_menu_handle_t`

Внутренняя структура хэндла меню (opaque-тип, доступен только через API компонента):

| Поле | Тип | Описание |
|------|-----|----------|
| `menu_stack` | `zh_menu_node_t *[ZH_MENU_MAX_DEPTH]` | Стек пройденных уровней меню для навигации `ZH_MENU_BACK` |
| `current_menu` | `zh_menu_node_t *` | Указатель на текущий активный уровень меню |
| `root` | `zh_menu_node_t *` | Корневой узел дерева меню, установленный при инициализации |
| `change_cb` | `void (*)(zh_menu_node_t *, uint8_t)` | Обратный вызов при изменении текущей позиции |
| `enter_cb` | `void (*)(void *)` | Обратный вызов при входе в меню (`ZH_MENU_ENTER`) |
| `exit_cb` | `void (*)(void *)` | Обратный вызов при выходе из меню (`ZH_MENU_BACK` с deepest level) |
| `loop` | `bool` | Флаг циклической навигации по элементам |
| `menu_depth` | `uint8_t` | Текущая глубина вложенности (0 — корневой уровень) |
| `current_index` | `uint8_t` | Индекс выделенного элемента на текущем уровне |
| `is_activated` | `bool` | Флаг активации меню (true после `ZH_MENU_ENTER`) |
| `is_selected` | `bool` | Флаг состояния выбора (true — режим корректировки выбираемого элемента) |
| `lock` | `portMUX_TYPE` | Спинлок для защиты полей от одновременного доступа |

### Структура узла меню `zh_menu_node_t`

Описывает один элемент меню, определённый пользователем в статическом массиве:

| Поле | Тип | Описание |
|------|-----|----------|
| `label` | `const char *` | Отображаемое имя элемента |
| `submenu` | `struct zh_menu_node_t *` | Указатель на массив вложенных элементов или `NULL` |
| `item_numbers` | `uint8_t` | Количество элементов в массиве `submenu` |
| `item_cb` | `void (*)(void *)` | Обратный вызов при выборе элемента |
| `selectable` | `bool` | Флаг: элемент может входить в режим ручной корректировки |

Пользователь обязан обеспечить время жизни массива узлов меню на протяжении всего времени работы меню — компонент не копирует узлы.

### Конфигурация инициализации `zh_menu_init_config_t`

Передаётся в `zh_menu_init()` для настройки меню:

| Поле | Тип | Описание |
|------|-----|----------|
| `root` | `zh_menu_node_t *` | Корневой узел дерева меню (must not be NULL) |
| `change_cb` | `void (*)(zh_menu_node_t *, uint8_t)` | Колбэк изменения позиции (может быть NULL) |
| `enter_cb` | `void (*)(void *)` | Колбэк входа в меню (может быть NULL) |
| `exit_cb` | `void (*)(void *)` | Колбэк выхода из меню (может быть NULL) |
| `loop` | `bool` | Флаг циклической навигации |
| `stack_size` | `uint16_t` | Размер стека задачи обработки (минимум `configMINIMAL_STACK_SIZE`) |
| `task_priority` | `uint8_t` | Приоритет задачи обработки (минимум 1) |
| `queue_size` | `uint8_t` | Размер очереди команд (минимум 1) |

### Статистика `zh_menu_stats_t`

Возвращается `zh_menu_get_stats()` для мониторинга:

| Поле | Тип | Описание |
|------|-----|----------|
| `queue_overflow_error` | `uint32_t` | Счётчик переполнений очереди команд |
| `min_stack_size` | `uint32_t` | Минимальный остаток свободного стека задачи (watermark) |

### Задача обработки и очередь

Компонент создаёт фоновую задачу FreeRTOS (`zh_menu`) и очередь сообщений (`_queue_handle`), которые разделяются между всеми инициализированными меню. Параметры (приоритет, размер стека, размер очереди) берутся из конфигурации **первого** инициализированного меню и применяются ко всем последующим. При деинициализации последнего меню задача и очередь удаляются.

Очередь передаёт команды навигации из контекста вызова публичных функций в фоновую задачу:

| Поле | Тип | Описание |
|------|-----|----------|
| `command` | `zh_menu_navigate_t` | Команда навигации |
| `handle` | `zh_menu_handle_t *` | Указатель на хэндл меню |
| `arg_size` | `uint32_t` | Размер аргумента в байтах |
| `arg` | `uint8_t[ZH_MENU_ARG_MAX_SIZE]` | Буфер аргумента, передаваемого в пользовательские колбэки |

## API (интерфейс)

### Инициализация и освобождение

| Функция | Описание |
|---------|----------|
| `zh_menu_init()` | Создаёт хэндл меню, проверяет конфигурацию, выделяет память и готовит внутреннее состояние. Меню переходит в неактивное состояние до отправки команды `ZH_MENU_ENTER`. Требует, чтобы входной `*handle` указывал на `NULL`. |
| `zh_menu_deinit()` | Освобождает память хэндла и устанавливает указатель в `NULL`. При деинициализации последнего меню удаляет общую задачу обработки и очередь. |

### Команды навигации

| Функция | Описание |
|---------|----------|
| `zh_menu_set()` | Отправляет команду навигации (`ZH_MENU_ENTER`, `ZH_MENU_SHOW`, `ZH_MENU_UP`, `ZH_MENU_DOWN`, `ZH_MENU_SELECT`, `ZH_MENU_BACK`) в фоновую задачу обработки. Невалидные команды в текущем состоянии игнорируются без ошибки. |

### Получение состояния

| Функция | Описание |
|---------|----------|
| `zh_menu_get()` | Заполняет структуру `zh_menu_status_t` флагами активности меню и состояния выбора текущего элемента. |

### Статистика

| Функция | Описание |
|---------|----------|
| `zh_menu_get_stats()` | Возвращает read-only указатель на глобальную структуру статистики `zh_menu_stats_t`. Указатель действителен до вызова `zh_menu_reset_stats()`. |
| `zh_menu_reset_stats()` | Обнуляет все счётчики статистики: переполнения очереди и watermark стека. |

### Типы

| Элемент | Описание |
| --------- | ---------- |
| `zh_menu_handle_t` | Opaque-хэндл меню, представляющий состояние меню. Все операции выполняются через указатель на этот тип. |
| `zh_menu_navigate_t` | Перечисление команд навигации: `ZH_MENU_ENTER`, `ZH_MENU_SHOW`, `ZH_MENU_UP`, `ZH_MENU_DOWN`, `ZH_MENU_SELECT`, `ZH_MENU_BACK`, `ZH_MENU_MAX`. |
| `zh_menu_node_t` | Узел дерева меню, определённый пользователем в статическом массиве. |
| `zh_menu_init_config_t` | Конфигурация инициализации: корневой узел, колбэки, флаг циклической навигации, параметры задачи. |
| `zh_menu_status_t` | Текущее состояние меню: `is_active` и `is_selected`. |
| `zh_menu_stats_t` | Статистика: `queue_overflow_error` и `min_stack_size`. |
| `ZH_MENU_INIT_CONFIG_DEFAULT()` | Макрос для инициализации конфигурации по умолчанию: приоритет 1, стек `configMINIMAL_STACK_SIZE`, размер очереди 1. |

## Потокобезопасность

1. **Спинлок (`portMUX_TYPE`)** — защищает поля внутреннего хэндла (`current_menu`, `current_index`, `is_activated`, `is_selected` и другие) от одновременного доступа из фоновой задачи обработки и публичных функций API. В `zh_menu_set()` и `zh_menu_get()` доступ к полям обернут в `taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()`.
2. **Очередь FreeRTOS** — команды навигации передаются из пользовательского контекста в фоновую задачу через очередь `xQueueSend()` с таймаутом 10 мс. Переполнение очереди фиксируется счётчиком `queue_overflow_error`.
3. **Фоновая задача** — вся логика навигации и вызовы пользовательских колбэков (`change_cb`, `enter_cb`, `exit_cb`, `item_cb`) выполняются в отдельной задаче FreeRTOS, что исключает блокировки в вызывающем коде. Колбэки вызываются вне критической секции.
4. **Разделяемые ресурсы** — задача обработки и очередь общие для всех инициализированных меню; их создание и удаление синхронизируется счётчиком `_menu_counter`.

Публичные функции не защищены мьютексом между вызовами из разных задач — их следует вызывать из одной задачи или обеспечивать внешнюю синхронизацию.

## Алгоритмы

### Навигация по уровням меню

При каждой команде навигации фоновая задача выполняет следующие шаги:

1. Чтение команды из очереди и извлечение хэндла меню.
2. `taskENTER_CRITICAL()` — блокировка спинлока для чтения текущего состояния.
3. Выполнение перехода по `switch (command)`:
   - `ZH_MENU_ENTER` — установка `is_activated = true`, `current_menu = root`, `current_index = 0`, очистка стека. Вызов `enter_cb`.
   - `ZH_MENU_SHOW` — принудительный вызов `change_cb` для рендеринга текущего состояния (только если меню активно и элемент не в режиме выбора).
   - `ZH_MENU_UP` / `ZH_MENU_DOWN` — инкремент/декремент `current_index` с учётом циклической навигации (`loop`). При циклическом режиме переход с первого на последний элемент и наоборот.
   - `ZH_MENU_SELECT` — если элемент `selectable`, вход в режим выбора (`is_selected = true`); если элемент имеет `submenu`, спуск в подменю (сохранение текущего уровня в `menu_stack`); вызов `item_cb`.
   - `ZH_MENU_BACK` — если в режиме выбора, выход из него; если есть предыдущий уровень в стеке, возврат (`menu_depth--`, восстановление `current_menu` из `menu_stack`); если стек пуст, деактивация меню и вызов `exit_cb`.
4. `taskEXIT_CRITICAL()` — разблокировка спинлока.
5. Вызов накопленных колбэков вне критической секции.

### Режим выбора (`is_selected`)

Когда пользователь выделяет элемент с флагом `selectable` и отправляет `ZH_MENU_SELECT`, меню переходит в режим выбора:

- В этом режиме команды `UP`, `DOWN`, `SHOW`, `ENTER` игнорируются.
- Допустимы только `ZH_MENU_SELECT` (повторный вызов `item_cb`) и `ZH_MENU_BACK` (выход из режима выбора).
- Это позволяет использовать энкодер для ручной корректировки значения элемента без вмешательства навигации по меню.

## Примеры использования

### Базовый пример: создание меню с энкодером

```c
#include "zh_menu.h"
#include "zh_encoder.h" // https://github.com/aZholtikov/zh_encoder

#define ENCODER_NUMBER 0x01

zh_encoder_handle_t *encoder_handle = NULL;
zh_menu_handle_t *menu_handle = NULL;

void zh_encoder_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data);
void zh_menu_enter_callback(void *arg);
void zh_menu_exit_callback(void *arg);
void zh_menu_change_callback(zh_menu_node_t *node, uint8_t index);
void zh_menu_selectable_callback(void *arg);
void zh_menu_simple_callback(void *args);

// Определение дерева меню
zh_menu_node_t main_menu = {
    .label = "Main menu",
    .submenu = (zh_menu_node_t[]){
        {
            .label = "Settings",
            .submenu = (zh_menu_node_t[]){
                {
                    .label = "Brightness",
                    .item_cb = &zh_menu_selectable_callback,
                    .selectable = true,
                },
                {
                    .label = "Volume",
                    .item_cb = &zh_menu_selectable_callback,
                    .selectable = true,
                },
                {
                    .label = "Back",
                    .item_cb = NULL, // нет колбэка для возврата
                },
            },
            .item_numbers = 3,
        },
        {
            .label = "Info",
            .submenu = (zh_menu_node_t[]){
                {
                    .label = "Version",
                    .item_cb = &zh_menu_simple_callback,
                },
                {
                    .label = "Back",
                    .item_cb = NULL,
                },
            },
            .item_numbers = 2,
        },
        {
            .label = "Exit",
            .item_cb = NULL,
        },
    },
    .item_numbers = 3,
};

void app_main(void)
{
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        ZH_ENCODER, ESP_EVENT_ANY_ID, &zh_encoder_event_handler, NULL, NULL));

    // Инициализация энкодера
    zh_encoder_init_config_t encoder_config = ZH_ENCODER_INIT_CONFIG_DEFAULT();
    encoder_config.a_gpio_number = GPIO_NUM_4;
    encoder_config.b_gpio_number = GPIO_NUM_16;
    encoder_config.s_gpio_number = GPIO_NUM_15;
    encoder_config.encoder_number = ENCODER_NUMBER;
    zh_encoder_init(&encoder_config, &encoder_handle);

    // Инициализация меню
    zh_menu_init_config_t menu_config = ZH_MENU_INIT_CONFIG_DEFAULT();
    menu_config.root = &main_menu;
    menu_config.loop = true;
    menu_config.enter_cb = &zh_menu_enter_callback;
    menu_config.exit_cb = &zh_menu_exit_callback;
    menu_config.change_cb = &zh_menu_change_callback;
    ESP_ERROR_CHECK(zh_menu_init(&menu_config, &menu_handle));

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    // По завершении работы: zh_menu_deinit(&menu_handle);
}

// Обработчик событий энкодера
void zh_encoder_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    (void)arg;
    zh_menu_status_t status = {0};
    zh_menu_get(&menu_handle, &status);

    if (event_id == ZH_BUTTON_EVENT) {
        zh_encoder_button_event_on_isr_t *btn = event_data;
        if (btn->button_status == true) {
            if (!status.is_active) {
                zh_menu_set(&menu_handle, ZH_MENU_ENTER, NULL, 0);
            } else if (!status.is_selected) {
                zh_menu_set(&menu_handle, ZH_MENU_SELECT, NULL, 0);
            } else {
                zh_menu_set(&menu_handle, ZH_MENU_BACK, NULL, 0);
            }
        }
    } else if (event_id == ZH_ENCODER_EVENT && status.is_active) {
        zh_encoder_event_on_isr_t *enc = event_data;
        if (status.is_selected) {
            zh_menu_set(&menu_handle, ZH_MENU_SELECT, enc, sizeof(zh_encoder_event_on_isr_t));
        } else {
            zh_menu_navigate_t cmd = enc->encoder_status ? ZH_MENU_UP : ZH_MENU_DOWN;
            zh_menu_set(&menu_handle, cmd, NULL, 0);
        }
    }
}

void zh_menu_enter_callback(void *arg)
{
    (void)arg;
    printf("Menu activated.\n");
}

void zh_menu_exit_callback(void *arg)
{
    (void)arg;
    printf("Menu deactivated.\n");
}

void zh_menu_change_callback(zh_menu_node_t *node, uint8_t index)
{
    printf("\n=== %s ===\n", node->label);
    for (uint8_t i = 0; i < node->item_numbers; ++i) {
        printf("%c %s\n", i == index ? '>' : ' ', node->submenu[i].label);
    }
    fflush(stdout);
}

void zh_menu_selectable_callback(void *arg)
{
    if (arg != NULL) {
        zh_encoder_event_on_isr_t *enc = arg;
        printf("Adjustable value changed by encoder %d: %0.0f\n",
               enc->encoder_number, enc->encoder_position);
    }
}

void zh_menu_simple_callback(void *args)
{
    (void)args;
    printf("Simple item action performed.\n");
}
```

### Мониторинг статистики

```c
#include "zh_menu.h"

zh_menu_handle_t *menu_handle = NULL;

void app_main(void)
{
    zh_menu_init_config_t config = ZH_MENU_INIT_CONFIG_DEFAULT();
    config.root = /* корневой узел меню */ NULL;
    zh_menu_init(&config, &menu_handle);

    for (;;) {
        const zh_menu_stats_t *stats = zh_menu_get_stats();
        if (stats->queue_overflow_error > 0) {
            printf("Queue overflow: %lu\n", (unsigned long)stats->queue_overflow_error);
        }
        if (stats->min_stack_size < 512) {
            printf("Low stack: %lu\n", (unsigned long)stats->min_stack_size);
        }
        zh_menu_reset_stats();
        vTaskDelay(pdMS_TO_TICKS(60000));
    }
    // По завершении работы: zh_menu_deinit(&menu_handle);
}
```

## Логирование

Компонент использует ESP-IDF логирование с тегом `"zh_menu"`. Сообщения об ошибках включают имя файла, номер строки и имя кода ошибки.

```c
esp_log_level_set("zh_menu", ESP_LOG_ERROR);
```

## Ошибки

| Код | Описание |
| ----- | ---------- |
| `ESP_OK` | Операция выполнена успешно |
| `ESP_ERR_INVALID_ARG` | NULL-аргументы, неверные параметры конфигурации (приоритет, размер стека, размер очереди, корневой узел), команда >= ZH_MENU_MAX, размер аргумента превышает лимит |
| `ESP_ERR_INVALID_STATE` | Меню по входному хэндлу уже инициализировано (`*handle != NULL`) |
| `ESP_ERR_NO_MEM` | Не удалось выделить память под хэндл меню |
| `ESP_FAIL` | Ошибка инициализации ресурсов или задачи обработки |

## Ограничения

1. **Максимальная глубина вложенности — 8 уровней** — ограничена константой `ZH_MENU_MAX_DEPTH`; при достижении лимита спуск в подменю не выполняется для предотвращения переполнения стека.
2. **Задача и очередь общие для всех меню** — параметры задачи (приоритет, размер стека, размер очереди) берутся из конфигурации первого инициализированного меню и применяются ко всем последующим; для последующих меню они игнорируются.
3. **Узлы меню не копируются** — пользователь обязан обеспечить время жизни массива `zh_menu_node_t` на протяжении всего времени работы меню.
4. **Ёмкость очереди по умолчанию равна 1** — при быстрой навигации возможны потери команд; факт потери фиксируется счётчиком `queue_overflow_error`.
5. **Колбэки вызываются вне критической секции** — пользовательские функции (`change_cb`, `enter_cb`, `exit_cb`, `item_cb`) не должны выполнять длительные блокирующие операции.
6. **Размер аргумента ограничен 128 байтами** — буфер `arg` в очереди команд имеет фиксированный размер `ZH_MENU_ARG_MAX_SIZE`.
7. **Публичные функции не защищены мьютексом** — их следует вызывать из одной задачи или обеспечивать внешнюю синхронизацию при доступе из нескольких контекстов.

## Требования для сборки

- ESP-IDF версии **5.0 или выше** — задачи FreeRTOS, очереди, спинлоки.
- FreeRTOS — фоновая задача обработки команд меню.

## Установка

```bash
cd ваш_проект/components
git clone https://github.com/aZholtikov/zh_menu
```

```c
#include "zh_menu.h"
```
