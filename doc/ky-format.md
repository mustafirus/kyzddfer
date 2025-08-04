# Формальний Опис Мови Метаданих "ky-format" (.ky)

**Версія:** 1.0
**Rev:** 16
**Дата:** 3 липня 2025 р.

## 1. Концепція та Призначення

**`ky-format`** — це гнучка, декларативна мова для опису метаданих систем. Вона використовує текстовий формат з ієрархічною структурою, заснованою на відступах, для опису складних програмних сутностей, таких як схеми баз даних (`tables`) та модульні додатки з їхніми лейаутами (`apps`). Ключова особливість формату — синтаксис визначення елемента залежить від контексту (секції), в якому він знаходиться.

## 2. Базовий Синтаксис

* **Розширення файлу:** `.ky`
* **Кодування:** `UTF-8`
* **Коментарі:** Будь-який текст після символу `#` і до кінця рядка вважається коментарем та ігнорується.
* **Ієрархія та Відступи:** Структура документу визначається відступами. Кожен рівень вкладеності — це **рівно 2 пробіли**.
* **Кореневий елемент:** Кожен `.ky` файл повинен починатися з кореневого елемента `rack`, який обов'язково має атрибут версії, наприклад `ver(1.0)` і закінчуватися на новим рядком(\n).
* **Структура секцій:** Всередині `rack` повинні бути присутні дві секції у строгому порядку: спочатку `tables`, а потім `apps`.
* **Неявне поле `id` в таблицях:** В ky-format немає поля `id`, фреймворк створює його в SQL самостійно як `id serial primary key`.

## 3. Визначення Сутностей

Синтаксис для визначення сутностей є контекстно-залежним.

### 3.1. Визначення Таблиці (у секції `tables`)

Таблиця визначається своїм іменем та необов'язковими прапорами й атрибутами.
* **Синтаксис:**
    ```text
    <tablename> [!flags] [attributes]
    ```
* **Приклад:**
    ```ky
    rack ver(1.0)
      tables
        users !auditable,log_changes owner(admin)
    ```

### 3.2. Визначення Поля (всередині таблиці)

Поле завжди має ім'я та тип, а також необов'язкові прапори й атрибути.
* **Синтаксис:**
    ```text
    <fieldname> type [!flags] [attributes]
    ```
* **Приклад:**
    ```ky
    tables
      users
        email varchar(100) !required,searchable,unique validate_re([[.*@.*]])
    ```

### 3.3. Визначення Додатку та Представлення (у секції `apps`)

Додатки (`app`) визначаються своїм іменем. Кожен додаток містить одне або більше представлень (лейаутів).
Визначення лейауту складається з двох частин, що йдуть одна за одною на одному рівні вкладеності:
1.  **Рядок метаданих:** Визначає назву (`<layoutname>`) та атрибути для вибору лейауту, такі як `pri`, `group`, `media`.
2.  **Рядок кореневого вузла:** Описує кореневий вузол (`<root>`) цього лейауту.
Це може бути будь-який вузол, як-от `box`, `list` або `form`.
* **Синтаксис:**
    ```text
    apps
      <appname> [!flags] [attributes]
        <layoutname> [!flags] [attributes(pri, group, media, ...)]
        <root_nodetag> [!flags] [attributes]
          ... дочірні вузли кореневого елемента ...
    ```
* **Приклад:**
    ```ky
    apps
      project_tracker

        # 1. Рядок метаданих для лейауту 'projects_view'.
        projects_view pri(1), media(desktop), title("Список проєктів")
        
        # 2. Рядок з кореневим вузлом (list) для 'projects_view'.
        # Цей вузол та його дочірні елементи є тілом лейауту.
        list table(projects)
          manager
          name
          end_date

        # Наступний лейаут для того ж додатку
        project_details_view pri(2)
        form table(projects)
          name
          description
    ```
### 3.4. Визначення Вузла Лейауту (всередині представлення)

Вузол лейауту визначається **тегом**, який вказує на його функціональність (напр., `box`, `list`), та необов'язковими прапорами й атрибутами.
* **Синтаксис:**
    ```text
    <tag> [!flags] [attributes]
    ```
* **Приклад:**
    ```ky
    apps
      user_management
        main_view
          box !container size(full_width)
    ```

## 4. Детальний Синтаксис Компонентів

### 4.1. Типи Полів (`type`)

* **Синтаксис:** `typename` або `typename(typeargs)`.
* **Приклади:** `text`, `varchar(150)`, `ref(users)`.

### 4.2. Прапори (`!flags`)

* **Синтаксис:** Блок починається з `!`, прапори перелічуються через кому **без пробілів**.
* **Приклад:** `!required,unique`.

### 4.3. Атрибути (`attributes`)

* **Синтаксис:**
    * Блок прапорів (якщо він є) відділяється від першого атрибута **пробілом**.
    * Атрибути між собою розділяються **комою** (з можливими пробілами навколо неї).
* **Приклад:** `status varchar(50) !required default(To Do), check(value > 0)`
* **Значення (`value`):**
    1.  **Стандартні дужки `( ... )`**: Для простих значень. Дозволяють переноси рядків.
    2.  **Альтернативні дужки `([[ ... ]])`**: Для "сирих" значень, що містять спецсимволи.
        * **Формат:** `атрибут([[ ... довільний вміст ... ]])`

## 5. Спеціалізовані Конструкції

* **Розширення Таблиць (`table_extension`)(PLANNED):** Визначається в секції `apps` для додавання полів до існуючих таблиць.
    ```ky
    apps
      user_profiles
        table_extension(users)
          bio text
    ```

## 6. Роль Парсера та Фабрики Об'єктів

Парсер аналізує `.ky` файл і, спільно з фабрикою об'єктів, перетворює його текстовий опис в ієрархію C++ об'єктів у пам'яті. Розуміння цього процесу є ключовим для роботи з фреймворком.

### Відповідність синтаксису `.ky` і класів C++

Нижче наведено таблицю, що показує, як елементи `.ky` файлу перетворюються на C++ об'єкти.

| Елемент у `.ky` файлі | Пов'язаний C++ Клас/Структура | Пояснення |
| :--- | :--- | :--- |
| `rack ver(...)` | `ky::Rack` | Глобальний об'єкт-синглтон, що є "контейнером" для всіх метаданих. Парсер очікує `rack` як кореневий вузол, отримує доступ до синглтона через `Rack::get()` і наповнює його поля `tables` та `apps`. |
| `  tables` | `ky::namemap<ky::Table>` | Вся секція `tables` слугує для наповнення колекції `Rack::tables`. |
| `    users` | `ky::Table` | Створюється об'єкт `ky::Table` з полем `name = "users"`. Цей об'єкт додається в `Rack::tables`. |
| `      full_name varchar(150)` | `ky::Field` + `ky::varchar_t` | Створюється `ky::Field` з `name = "full_name"`. Його поле `type` вказує на об'єкт `ky::varchar_t`, створений через `type_t::fabric` з `maxlen = 150`. Готовий `Field` додається в `Table::fields` поточної таблиці. |
| `      manager ref(users)` | `ky::Field` + `ky::ref_t` | Створюється `ky::Field` з `name = "manager"`. Його поле `type` вказує на `ky::ref_t`, де `table_ref` буде посиланням на об'єкт `Table` з іменем `users`. |
| `      age int` | `ky::Field` + `ky::int_t` | Створюється `ky::Field` з `name = "age"`, а його `type` вказує на об'єкт `ky::int_t`. |
| `  apps` | `ky::namemap<ky::App>` | Вся секція `apps` слугує для наповнення колекції `Rack::apps`. |
| `    project_tracker` | `ky::App` | Створюється екземпляр `App`, в який будуть додані наступні лейаути. |
| `      projects_view pri(1)` | `ky::Layout` | Створюється об'єкт `Layout` для опису шаблону. Атрибути записуються у відповідні поля (`pri`, `group`, `media`) та в `attrs`. |
| `      list table(projects)` | `ky::LayoutNodeList` | Створюється екземпляр `LayoutNodeList`, який стає кореневим вузлом (`root_node`) для попередньо створеного об'єкта `Layout`. |
| `        name` (у `list`) | `ky::LayoutField` | Створює об'єкт `LayoutField` і додається до колекції `fields` батьківського вузла (`LayoutNodeList`). |
| `        hbox` (у `form`) | `ky::LayoutNodeBox` | Вкладений вузол створює екземпляр свого класу і додається до колекції `nodes` батьківського вузла. |

## 7. Формальна граматика ky-format (BNF-подібна нотація)

### 1. Термінали (Лексеми)

Це базові елементи, які розпізнає сканер (лексичний аналізатор).

| Термінал | Опис | Приклад |
| :--- | :--- | :--- |
| `IDENTIFIER` | Послідовність літер, цифр та `_` | `users`, `full_name`, `varchar` |
| `VALUE` | Довільний текст всередині окрім дужок та нового рядка `( )` | `To Do`, `project_id = ...` |
| `RAW_VALUE` | Довільний текст всередині окрім `([[` та `]])` | `^(?=.*[a-z])(?=.*[A-Z])(?=.*\d)(?=.*[@$!%*?&])[A-Za-z\d@$!%*?&]{8,16}$` |
| `INDENT` | Два пробіли на початку рядка | `  ` |
| `NEWLINE` | Символ нового рядка | `\n` |
| `COMMENT` | Символ `#` і весь текст до кінця рядка | `# це коментар` |
| `COMMA` | Символ коми | `,` |
| `FLAG_START` | Символ оклику | `!` |
| `L_PAREN`, `R_PAREN` | Ліва та права круглі дужки | `(`, `)` |
| `RAW_L_PAREN`, `RAW_R_PAREN` | Ліва та права альтернативні дужки | `([[`, `]])` |

### 2. Не-термінали (Правила Граматики)

```bnf
// -- Структура файлу --

// Файл .ky - це єдиний вузол 'rack', що містить атрибут версії.
ky_file ::= rack_def

// <rack> ver(X.Y) ...
// Всередині rack обов'язково йдуть секції tables, а потім apps.
rack_def ::= "rack" attribute_list NEWLINE tables_section apps_section NEWLINE

// Секції тепер мають відступ в один рівень (1 INDENT)
tables_section ::= INDENT "tables" NEWLINE (table_def)+
apps_section   ::= INDENT "apps" NEWLINE (app_def)+


// -- Визначення сутностей --

// <tablename> [!flags] [attributes] (2 відступи)
table_def ::= INDENT INDENT IDENTIFIER optional_tail NEWLINE (field_def)*

// <fieldname> type [!flags] [attributes] (3 відступи)
field_def ::= INDENT INDENT INDENT IDENTIFIER type_def optional_tail NEWLINE

// <appname> [!flags] [attributes] (2 відступи)
app_def ::= INDENT INDENT IDENTIFIER optional_tail NEWLINE (layout_def)+

// Визначення леяуту.
// Складається з рядка метаданих та кореневого вузла,
// що є сусідами на одному рівні вкладеності (3 відступи).
layout_def ::= layout_metadata_line layout_node

// Рядок метаданих для леяуту, e.g., "   projects_view pri(1)"
layout_metadata_line ::= INDENT INDENT INDENT IDENTIFIER optional_tail NEWLINE

// Вузол леяуту.
// Це правило рекурсивне і обробляє будь-який рівень вкладеності,
// починаючи з кореневого вузла (3 відступи).
// (INDENT)+ означає "один або більше токенів INDENT".
layout_node ::= (INDENT)+ IDENTIFIER optional_tail NEWLINE (layout_node)*


// -- Загальні компоненти --

// Порядок: !flags, потім attributes, розділені пробілом 
optional_tail ::= (FLAG_START flags_list)? (" " attribute_list)?

// Блок прапорів, розділених комою без пробілів
flags_list ::= IDENTIFIER (COMMA IDENTIFIER)*

// Список атрибутів, розділених комою
attribute_list ::= attribute (COMMA attribute)*

// name(value) або name([[raw_value]])
attribute ::= IDENTIFIER value_container

value_container ::= L_PAREN VALUE R_PAREN 
                  | RAW_L_PAREN RAW_VALUE RAW_R_PAREN

// typename або typename(args)
type_def ::= IDENTIFIER (L_PAREN VALUE R_PAREN)?
```

## 8. Повний Зведений Приклад

```ky
# файл: my_system.ky

rack ver(1.0)
  tables
    # Основна таблиця користувачів
    users
      full_name varchar(150) !required
      email varchar(100) !required,unique validate_re([[^\\w+@\\w+\\.\\w+$]])
      role varchar(50) !required
      created_at timestamp default(now)
    
    # Таблиця проєктів
    projects
      name varchar(200) !required,unique
      description text
      start_date date
      end_date date
      manager ref(users)
    
    # Таблиця завдань
    tasks
      project ref(projects) !required
      author ref(users)
      title varchar(255) !required
      description text
      status varchar(50) !required default(To Do)
      priority varchar(50) !required default(Medium)
      due_date date
    
    # Таблиця для зв'язку завдань та виконавців
    task_assignees unique(task, user)
      task ref(tasks) !required
      user ref(users) !required
    
    # Таблиця коментарів до завдань
    comments
      task ref(tasks) !required
      user ref(users) !required
      content text !required
      created_at timestamp default(now)

    # Таблиця для запису витраченого часу
    time_logs
      task ref(tasks) !required
      user ref(users) !required
      hours_spent numeric(5, 2) !required check(hours_spent > 0)
      log_date date !required default(current_date)

  apps
    # Додаток для відстеження проєктів
    project_tracker !active version(1.0)
      
      # Головне представлення зі списком проєктів.
      # Рядок нижче - це метадані лейауту.
      projects !default title("Проєкти")
      # А цей рядок - його кореневий вузол (root_node).
      # Вони знаходяться на одному рівні вкладеності.
      list table(projects)
        manager
        name
        end_date

      # Представлення з детальною інформацією про проєкт.
      # Знову, спочатку метадані...
      project_details
      # ...а потім кореневий вузол на тому ж рівні.
      form table(projects)
        hbox
          fieldbox title("Інформація про проєкт")
            name
            description
            manager
          list table(tasks), title("Завдання проєкту"), filter([[[project_id = {current_project_id}]]])
            title
            status
            priority
            due_date
```
