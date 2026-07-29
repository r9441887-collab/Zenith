# Zenith — игровой язык программирования

**Zenith** — компилируемый язык программирования для разработки игр.
Вдохновлён синтаксисом Swift и Python. Компилируется напрямую в машинный код x86_64
и упаковывается в Portable Executable (.exe) без единой внешней зависимости.

**Текущая версия:** v2.0.0 (Стабильный релиз)

```
Source.z -> Lexer -> Parser -> Optimizer -> Codegen -> .exe
              (C++)    (AST)    (dead code)  (x86_64 PE)
```

---

## Содержание

- [Сборка](#сборка)
- [Использование](#использование)
- [Синтаксис языка](#синтаксис-языка)
- [Как работает изнутри](#как-работает-изнутри)
- [Встроенные функции](#встроенные-функции)
- [Встроенные типы и структуры](#встроенные-типы-и-структуры)
- [2D-графика (GUI)](#2d-графика-gui)
- [Аллокаторы памяти](#аллокаторы-памяти)
- [Интероперабельность с C-библиотеками](#интероперабельность-с-c-библиотеками)
- [Архитектура компилятора](#архитектура-компилятора)
- [Оптимизатор (Dead Code Elimination)](#оптимизатор-dead-code-elimination)
- [Генерация машинного кода x86_64](#генерация-машинного-кода-x86_64)
- [Формат PE (.exe)](#формат-pe-exe)
- [Примеры](#примеры)
- [История исправлений](#история-исправлений)
- [Структура проекта](#структура-проекта)

---

## Сборка

### Требования

- MinGW-w64 (g++) или MSVC
- Стандарт C++17

### Компиляция компилятора

```bash
# MinGW
g++ -std=c++17 -o zenith.exe src/main.cpp src/lexer.cpp src/parser.cpp src/codegen.cpp src/optimizer.cpp

# MSVC
cl /EHsc /Fezenith.exe /Isrc src\main.cpp src\lexer.cpp src\parser.cpp src\codegen.cpp src\optimizer.cpp

# или просто
build.bat
```

Файлы проекта не используют внешних зависимостей — только стандартная библиотека C++.

---

## Использование

```bash
zenith <input.z> -o <output.exe>
```

| Аргумент | Описание |
|----------|----------|
| `input.z` | Входной файл исходного кода |
| `-o output` | Имя выходного .exe (по умолч. a.exe) |

Каждый файл `.z` **обязан** начинаться с директивы `app`, указывающей тип приложения:

```zenith
app console     # консольное приложение (базовые типы, print, alloc/free)
app gui         # GUI-приложение (все типы + 2D-графика)
```

Пример:

```bash
zenith examples/main.z -o game.exe
game.exe
echo %ERRORLEVEL%   # 8 (результат 5+3)
```

---

## Синтаксис языка

### Комментарии

```zenith
# это однострочный комментарий
```

Комментарии начинаются с `#` и продолжаются до конца строки.

### Переменные

```zenith
var health: int = 100
var name: string = "Player"

var score = 0          # вывод типа -> int
var speed = 2.5        # вывод типа -> float
```

### Массивы

```zenith
var arr: [10]int           # массив из 10 int на стеке
arr[0] = 42
var x: int = arr[0]       # чтение
```

Массивы фиксированного размера выделяются на стеке. Индексация:
- для `int` — `base + index * 8`
- для `float` — `base + index * 4`

### Структуры

```zenith
struct Vec2
    var x: float = 0.0
    var y: float = 0.0
end

struct Player
    var name: string
    var hp: int = 100
    var pos: Vec2
end
```

Поля структуры доступны через точку:

```zenith
var v: Vec2
v.x = 3.14
v.y = 2.71
```

### Hex-литералы

```zenith
var color: int = 0xFFFF0000    # красный (hex)
var mask: int = 0xFF           # 255
```

### Логическое НЕ

```zenith
var flag: bool = true
if !flag
    # ...
end
```

### Строковые литералы

Строки поддерживают экранирование:

| Escape | Описание |
|--------|----------|
| `\n` | Перевод строки |
| `\t` | Табуляция |
| `\r` | Возврат каретки |
| `\\` | Обратный слэш |
| `\"` | Кавычка |
| `\0` | Нулевой байт |

### Именованные цвета

В режиме `app gui` доступны предопределённые цвета:

| Имя | ARGB | Цвет |
|-----|------|------|
| `Red` | `0xFFFF0000` | Красный |
| `Green` | `0xFF00FF00` | Зелёный |
| `Blue` | `0xFF0000FF` | Синий |
| `White` | `0xFFFFFFFF` | Белый |
| `Black` | `0xFF000000` | Чёрный |
| `Yellow` | `0xFFFFFF00` | Жёлтый |
| `Cyan` | `0xFF00FFFF` | Голубой |
| `Magenta` | `0xFFFF00FF` | Маджента |
| `Gray` | `0xFF808080` | Серый |

### Функции

```zenith
func add(a: int, b: int) -> int
    return a + b
end

func update(delta: float)       # void (без return type)
    # game logic here
end
```

### Управляющие конструкции

#### if / else if / else

```zenith
if hp <= 0
    print("dead")
else if hp < 50
    print("wounded")
else
    print("alive")
end
```

#### while

```zenith
var i: int = 0
while i < 10
    print(i)
    i = i + 1
end
```

#### for

```zenith
for i = 0, 10          # i от 0 до 9 (шаг 1)
    print(i)
end

for i = 0, 10, 2      # i от 0 до 9 с шагом 2 (0, 2, 4, 6, 8)
    print(i)
end
```

Цикл счётчика с необязательным шагом. Синтаксис: `for var = start, end[, step]`.
Компилятор автоматически определяет направление: при отрицательном шаге условие выхода меняется с `jge` на `jle`.

---

## Как работает изнутри

Раздел описывает, как каждый аспект языка Zenith транслируется в x86_64 машинный код и
управляет памятью на уровне Windows PE.

### Стековый фрейм функции

Каждая функция при входе строит стековый фрейм:

```
                  [высшие адреса]
  ┌─────────────────────────────┐
  │ [rbp+48]  param 4 (r9)     │  ← 5-й и далее параметры через стек
  │ [rbp+40]  param 3 (r8)     │
  │ [rbp+32]  param 2 (rdx)    │
  │ [rbp+24]  param 1 (rcx)    │  ← shadow space (32 байта)
  │ [rbp+16]  return address   │  ← push рб call
  │ [rbp+8]   (padding)        │
  │ [rbp+0]   saved rbp        │  ← push rbp; mov rbp, rsp
  │ [rbp-8]   saved rbx        │  ← push rbx (используется как базовый регистр)
  │ [rbp-16]  local var 1      │
  │ [rbp-24]  local var 2      │
  │ ...                        │
  │ [rbp-N]   spill area       │  ← 4×8 = 32 байта для spill регистров
  └─────────────────────────────┘
                  [низкие адреса]
```

**Вызов функции выглядит так:**

```asm
; Вызов myFunc(1, 2, 3):
mov  ecx, 1              ; param 1 → rcx
mov  edx, 2              ; param 2 → rdx
mov  r8d, 3              ; param 3 → r8
sub  rsp, 0x28           ; shadow space (32 байта) + выравнивание (8)
call myFunc               ; push return address, jmp
add  rsp, 0x28           ; восстановить стек
```

**Пролог функции:**

```asm
myFunc:
  push rbp               ; сохранить старый rbp
  mov  rbp, rsp          ; установить базу фрейма
  push rbx               ; сохранить rbx (callee-saved)
  sub  rsp, N            ; выделить место под локальные переменные + spill
```

**Эпилог:**

```asm
  mov  eax, ecx          ; результат в rax (int)
  add  rsp, N            ; освободить локальные
  pop  rbx               ; восстановить rbx
  pop  rbp               ; восстановить rbp
  ret                    ; return
```

**Регистры:**
- Параметры: `rcx`, `rdx`, `r8`, `r9` (до 4 штук)
- Результат: `rax` (int/string/указатель) или `xmm0` (float)
- Используются: `rax`, `rcx`, `rdx`, `rbx` (4 GP-регистра), `xmm0`–`xmm7` (8 SSE)

### Переменные — как они хранятся

Все переменные живут на стеке в `[rbp-offset]`.

| Тип | Размер на стеке | Представление |
|-----|----------------|---------------|
| `int` | 8 байт | 64-bit signed integer |
| `float` | 4 байта | IEEE 754 single (SSE `movss`) |
| `bool` | 4 байта | 0 или 1 (32-bit) |
| `string` | 8 байт | RIP-relative указатель на `.rdata` |
| `vec2` | 8 байт | 2 × float (x, y) |
| `vec3` | 16 байт | 3 × float + 4 байта padding |
| `color` | 16 байт | 4 × float (r, g, b, a) |
| `entity` | 8 байт | Opaque 64-bit handle |
| struct | суммарный размер | Выровненный по полям |

**Пример:** `var x: int = 42` компилируется в:

```asm
mov qword [rbp-16], 42    ; записать 64-bit значение в стек
```

**Чтение переменной:**

```asm
mov rax, [rbp-16]          ; загрузить x в rax
```

### Массивы — вычисление адреса

Массивы фиксированного размера выделяются целиком на стеке.

```zenith
var arr[10]: int      # 10 × 8 = 80 байт на стеке
```

**Чтение `arr[i]`:**

```asm
lea  r10, [rbp-96]        ; базовый адрес массива (начало на стеке)
mov  rax, [rbp-104]       ; загрузить индекс i
imul rax, rax, 8          ; масштабировать: index × sizeof(int)
add  r10, rax             ; адрес элемента
mov  rax, [r10]           ; прочитать arr[i]
```

**Запись `arr[i] = 42`:**

```asm
lea  r10, [rbp-96]
mov  rax, [rbp-104]       ; индекс
imul rax, rax, 8
add  r10, rax
mov  qword [r10], 42      ; записать
```

Для `float` массивов: `imul rax, rax, 4` и `movss xmm0, [r10]`.

### Структуры — layout в памяти

Структуры упаковываются с выравниванием по размеру каждого поля.

```zenith
struct Player
    var name: string    # offset=0,  size=8
    var hp: int         # offset=8,  size=8
    var pos: vec2       # offset=16, size=8 (2 × float)
end
# totalSize = 24 байта
```

**Алгоритм `computeStructLayouts()`:**

```
offset = 0
для каждого поля:
    fieldSize = sizeof(field.type)
    if (offset % fieldSize != 0):
        offset += fieldSize - (offset % fieldSize)    # выравнивание
    fieldOffset[field.name] = offset
    offset += fieldSize
totalSize = offset
```

**Доступ к полю `player.hp`:**

```asm
mov rax, [rbp-24]         ; загрузить переменную player (базовый адрес)
mov rax, [rax+8]          ; прочитать поле hp (смещение 8)
```

**Запись `player.hp = 100`:**

```asm
mov rax, [rbp-24]         ; базовый адрес player
mov qword [rax+8], 100    ; записать в поле hp
```

### Функции — вызов и возврат

```zenith
func add(a: int, b: int) -> int
    return a + b
end
```

Компилируется в:

```asm
add:
  push rbp
  mov  rbp, rsp
  push rbx
  sub  rsp, 0x20           ; 32 байта (spill area)
  mov  [rbp+24], rcx       ; сохранить param a (из rcx)
  mov  [rbp+32], rdx       ; сохранить param b (из rdx)

  mov  rax, [rbp+24]       ; загрузить a
  add  rax, [rbp+32]       ; a + b
                           ; результат уже в rax — готово

  add  rsp, 0x20
  pop  rbx
  pop  rbp
  ret
```

**Вызов:** `add(5, 3)` → `mov ecx, 5; mov edx, 3; sub rsp, 0x28; call add; add rsp, 0x28`
Результат в `rax` = 8.

**Void-функции** не возвращают значения, `ret` без `mov eax, ...`.

### if / else — условные переходы

```zenith
if x > 0
    print("positive")
else
    print("non-positive")
end
```

Компилируется в:

```asm
  mov  rax, [rbp-16]       ; загрузить x
  cmp  rax, 0              ; сравнить x с 0
  mov  eax, 0              ; подготовить false
  jle  elseLabel           ; если x <= 0 → прыгнуть в else
  mov  eax, 1              ; then-блок: результат = true
  ; ... then-блок ...
  jmp  endLabel            ; перейти к концу

elseLabel:
  ; ... else-блок ...

endLabel:
```

**`else if`** парсится как рекурсивный `IfStmt` внутри else-блока:

```zenith
if x == 1
    # block 1
else if x == 2
    # block 2
else
    # block 3
end
```

= IfStmt(condition=x==1, then=block1, else=IfStmt(condition=x==2, then=block2, else=block3))

**Короткое замыкание `&&` и `||`:**

```asm
; a && b:
  ; вычислить a
  test rax, rax
  jz   falseLabel        ; a == 0 → сразу false (b не вычисляется)
  ; вычислить b
  test rax, rax
  jnz  trueLabel         ; b != 0 → true
falseLabel:
  mov  eax, 0
  jmp  endLabel
trueLabel:
  mov  eax, 1
endLabel:

; a || b:
  ; вычислить a
  test rax, rax
  jnz  trueLabel         ; a != 0 → сразу true (b не вычисляется)
  ; вычислить b
  test rax, rax
  jz   falseLabel        ; b == 0 → false
trueLabel:
  mov  eax, 1
  jmp  endLabel
falseLabel:
  mov  eax, 0
endLabel:
```

### while — цикл

```zenith
while i < 10
    i = i + 1
end
```

```asm
loopLabel:
  mov  rax, [rbp-16]       ; загрузить i
  cmp  rax, 10             ; сравнить с 10
  jge  endLabel            ; если i >= 10 → выход
  ; ... тело цикла ...
  inc  qword [rbp-16]      ; i = i + 1
  jmp  loopLabel            ; вернуться к проверке
endLabel:
```

### for — цикл счётчика

```zenith
for i = 0, 10
    print(i)
end
```

```asm
  mov  qword [rbp-16], 0           ; i = 0 (начальное значение)

loopLabel:
  mov  rax, [rbp-16]               ; загрузить i
  cmp  rax, 10                     ; сравнить i с end
  jge  endLabel                    ; выход если i >= end
  ; ... тело цикла ...
  inc  qword [rbp-16]              ; i += 1 (шаг по умолчанию = 1)
  jmp  loopLabel
endLabel:
```

**С шагом 2:**

```asm
  mov  qword [rbp-16], 0
loopLabel:
  mov  rax, [rbp-16]
  cmp  rax, 10
  jge  endLabel
  ; ... тело ...
  add  qword [rbp-16], 2           ; i += step
  jmp  loopLabel
endLabel:
```

**Отрицательный шаг** (`for i = 10, 0, -1`): компилятор меняет `jge` на `jle`,
поэтому цикл корректно отсчитывает вниз.

### Операторы — машинные инструкции

#### Арифметика целых

| Оператор | Инструкции |
|----------|-----------|
| `a + b` | `mov rax, a; add rax, b` |
| `a - b` | `mov rax, a; sub rax, b` |
| `a * b` | `mov rax, a; imul rax, b` |
| `a / b` | `mov rax, a; cqo; idiv rcx` ( знаковое деление) |
| `-a` | `neg rax` |

Деление: `cqo` знаково расширяет `rax` в `rdx:rax`, затем `idiv rcx` делит `rdx:rax` на `rcx`.
Результат в `rax` (частное), остаток в `rdx`.

#### Арифметика float

Все float-операции через SSE `xmm`-регистры:

| Оператор | Инструкция |
|----------|-----------|
| `a + b` | `addss xmm0, xmm1` |
| `a - b` | `subss xmm0, xmm1` |
| `a * b` | `mulss xmm0, xmm1` |
| `a / b` | `divss xmm0, xmm1` |
| int→float | `cvtsi2ss xmm0, rax` |
| float→int | `cvtss2si rax, xmm0` |

#### Сравнение целых

```asm
  mov  rax, a
  cmp  rax, b            ; устанавливает флаги (ZF, SF, OF, CF)
  mov  eax, 0            ; подготовить false
  jcc  endLabel          ; если условие НЕ выполнено → false
  mov  eax, 1            ; иначе true
endLabel:
```

| Оператор | Условный переход (jcc) | Значение |
|----------|----------------------|----------|
| `==` | `jne` | прыгнуть если НЕ равно |
| `!=` | `je` | прыгнуть если равно |
| `<` | `jge` | прыгнуть если больше или равно |
| `>` | `jle` | прыгнуть если меньше или равно |
| `<=` | `jg` | прыгнуть если больше |
| `>=` | `jl` | прыгнуть если меньше |

#### Сравнение float

```asm
  ucomiss xmm0, xmm1     ; сравнить float
  mov     eax, 0          ; false
  setcc   al              ; установить al = 1 если условие выполнено
  movzx   eax, al         ; расширить до 32-bit
```

| Оператор | Setcc |
|----------|-------|
| `==` | `setz` |
| `!=` | `setnz` |
| `<` | `setb` |
| `>` | `seta` |
| `<=` | `setbe` |
| `>=` | `setnb` |

#### Логические операторы

| Оператор | Инструкция |
|----------|-----------|
| `!a` | `test rax, rax; setz al; movzx eax, al` |
| `a && b` | Short-circuit через `jz` (см. раздел if/else) |
| `a \|\| b` | Short-circuit через `jnz` (см. раздел if/else) |

### print — вывод в консоль

`print()` — единственная функция вывода. Полиморфна: принимает строку ИЛИ целое число.

#### Печать строки

```zenith
print("Hello!")
```

```asm
  sub  rsp, 0x28
  mov  ecx, -11                   ; STD_OUTPUT_HANDLE
  call [rip + IAT_GetStdHandle]   ; получить handle.stdout
  mov  rbx, rax                   ; сохранить handle

  ; Напечатать строку
  mov  ecx, ebx                   ; handle
  lea  rdx, [rip + str_Hello]     ; указатель на "Hello!" в .rdata
  mov  r8d, 6                     ; длина строки
  xor  r9d, r9d                   ; NULL (lpNumberOfBytesWritten)
  mov  qword [rsp+32], 0          ; NULL (lpOverlapped)
  call [rip + IAT_WriteFile]      ; WriteFile(handle, "Hello!", 6, NULL, NULL)

  ; Напечатать \r\n
  mov  ecx, ebx
  lea  rdx, [rip + str_newline]   ; "\r\n"
  mov  r8d, 2
  call [rip + IAT_WriteFile]

  add  rsp, 0x28
```

#### Печать числа

```zenith
print(42)
```

Компилятор генерирует код конвертации числа в десятичную строку на стеке:

```asm
  ; Получить handle stdout (аналогично выше)
  ...

  ; Конвертация 42 → "42"
  mov  rax, 42
  xor  rcx, rcx                   ; счётчик цифр = 0
  mov  r10, 10                    ; делитель

convertLoop:
  xor  rdx, rdx
  div  r10                        ; rax = rax/10, rdx = rax%10
  add  dl, '0'                    ; остаток → ASCII цифра
  mov  byte [rsp+rcx], dl         ; сохранить цифру на стек (обратный порядок)
  inc  rcx
  test rax, rax
  jnz  convertLoop

  ; Развернуть строку (цифры были в обратном порядке)
  ; WriteFile(handle, digits, count, NULL, NULL)
  mov  ecx, ebx
  lea  rdx, [rsp]                 ; начало буфера цифр
  ; r8 = rcx (количество цифр)
  call [rip + IAT_WriteFile]

  ; Напечатать \r\n
  ...
```

**Отрицательные числа:** перед конвертацией проверяется знак, при необходимости
добавляется символ `-` и число берётся по модулю (`neg rax`).

**Число 0:** обрабатывается отдельно — сразу записывается ASCII `'0'`.

**`print()` всегда добавляет `\r\n`** после вывода.

### Типы данных — представление в памяти

| Тип | Размер | Регистр | Пример | Примечание |
|-----|--------|---------|--------|------------|
| `int` | 8 байт | `rax` | `42`, `0xFF` | 64-bit signed |
| `float` | 4 байта | `xmm0` | `3.14`, `1.0f` | IEEE 754 single |
| `bool` | 4 байта | `rax` | `true`/`false` | 1/0 как int |
| `string` | 8 байт | `rax` | `"hello"` | RIP-relative ptr → `.rdata` |
| `void` | 0 | — | — | Нет значения |
| `vec2` | 8 байт | стек | `{1.0, 2.0}` | `{x: float, y: float}` |
| `vec3` | 16 байт | стек | `{1, 2, 3}` | `{x, y, z: float}` + padding |
| `color` | 16 байт | стек | `{1, 0, 0, 1}` | `{r, g, b, a: float}` |
| `entity` | 8 байт | `rax` | — | Opaque handle (зарезервирован) |

**Hex-литералы:** `0xFFFF0000` парсятся как целое число (int64).

**Bool:** `true` = `1`, `false` = `0`. Нет отдельного bool-типа на уровне машинного кода —
это обычные целые 0 и 1.

### Строки и string pool

Все строковые литералы собираются в общий **string pool** в секции `.rdata`.

```
.stringPool:
  db "Hello!", 0           ; offset 0
  db "World", 0            ; offset 7
  db "\r\n", 0             ; offset 13
```

При обращении к строке компилятор генерирует RIP-relative LEA:

```asm
lea rax, [rip + displacement_to_string]
```

`displacement` вычисляется при fixup-проходе и указывает на нужный offset в stringPool.

**Экранирование:** `\n` → 0x0A, `\t` → 0x09, `\r` → 0x0D, `\\` → 0x5C, `\"` → 0x22, `\0` → 0x00.

### @import — IAT (Import Address Table)

Компилятор генерирует PE-импорты вручную, без линкера.

#### Структура для каждой DLL

```
.rdata:
  IMAGE_IMPORT_DESCRIPTOR (20 байт на DLL)
    -> ILT (Import Lookup Table) в .data
    -> DLL name string ("user32.dll")
    -> IAT (Import Address Table) в .data

  Hint/Name Table:
    2 байта hint + "FunctionName\0" (для каждой функции)

.data:
  ILT:  [RVA_to_hint1, RVA_to_hint2, ..., 0]    ; null-terminated
  IAT:  [slot1, slot2, ...]                       ; загрузчик заполнит адресами
```

#### Автоматический маппинг DLL

Если DLL не указана явно через `@import`, компилятор определяет её по имени функции:

| Имена функций | DLL |
|---------------|-----|
| `ExitProcess`, `GetStdHandle`, `WriteFile`, `ReadFile`, `Sleep`, `HeapAlloc`, `HeapFree`, `GetProcessHeap`, `GetModuleHandleA` | `kernel32.dll` |
| `CreateWindowExA`, `DefWindowProcA`, `RegisterClassExA`, `DestroyWindow`, `GetDC`, `ReleaseDC`, `PeekMessageA`, `TranslateMessage`, `DispatchMessageA`, `GetAsyncKeyState`, `PostQuitMessage`, `ShowWindow`, `UpdateWindow` | `user32.dll` |
| `CreateDIBSection`, `BitBlt`, `SelectObject`, `CreateCompatibleDC`, `DeleteObject` | `gdi32.dll` |
| `threadCreate`, `threadJoin`, `threadSleepMs` и т.д. | `libs_thread.dll` |
| Остальные | `kernel32.dll` |

#### Вызов extern-функции

```zenith
extern func MessageBoxA(h: int, text: int, caption: int, type: int) -> int
```

```asm
  mov  ecx, 0              ; hWnd = NULL
  lea  rdx, [rip + str_text]
  lea  r8,  [rip + str_caption]
  mov  r9d, 0              ; MB_OK
  sub  rsp, 0x28           ; shadow space
  call [rip + IAT_MessageBoxA]   ; косвенный вызов через IAT
  add  rsp, 0x28
```

### GUI — программный framebuffer

Режим `app gui` включает систему 2D-графики через software rendering.

#### createWindow(w, h, title)

Создаёт окно и framebuffer за один вызов. Генерирует ~200 инструкций:

```
1. GetModuleHandleA(NULL)        → hInstance
2. RegisterClassExA(&wndclass)   → регистрирует класс окна
3. CreateWindowExA(...)          → создаёт HWND
4. ShowWindow(hwnd, SW_SHOW)
5. UpdateWindow(hwnd)
6. GetDC(hwnd)                   → hdc (для Blitting)
7. CreateCompatibleDC(hdc)       → hdcMem (backbuffer)
8. CreateDIBSection(hdc, &bmi)   → hBitmap + framebuffer ptr
9. SelectObject(hdcMem, hBitmap) → подключить bitmap к DC
10. ReleaseDC(hwnd, hdc)
```

**DIB Section (32bpp BGRA):**

```c
BITMAPINFO bmi = {0};
bmi.bmiHeader.biSize = 40;
bmi.bmiHeader.biWidth = w;
bmi.bmiHeader.biHeight = -h;   // top-down
bmi.bmiHeader.biPlanes = 1;
bmi.bmiHeader.biBitCount = 32;
bmi.bmiHeader.biCompression = 0; // BI_RGB
void* pBits;
CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
```

**win32Globals layout (56 байт в `.data`):**

| Offset | Размер | Поле | Описание |
|--------|--------|------|----------|
| 0 | 8 | initFlag | 0 = не инициализировано, 1 = инициализировано |
| 8 | 8 | hwnd | HWND окна |
| 16 | 8 | hdcMem | Memory DC (backbuffer) |
| 24 | 8 | hBitmap | DIB Section bitmap |
| 32 | 4 | width | Ширина окна |
| 36 | 4 | height | Высота окна |
| 40 | 8 | framebuffer | Указатель на пиксели (BGRA) |

**Многократный вызов:** если `initFlag != 0`, `createWindow` пропускает всю инициализацию.

#### drawPixel(x, y, color)

```zenith
drawPixel(100, 200, 0xFFFFFFFF)
```

```asm
  mov  r10d, 0xFFFFFFFF          ; цвет (ARGB)
  mov  eax, [rbp-40]             ; y
  mov  r11d, [rbp+32+win32Globals] ; width (из win32Globals)
  imul rax, rax, r11             ; y * width
  add  rax, [rbp-48]             ; + x
  shl  rax, 2                    ; × 4 (4 байта на пиксель BGRA)
  mov  r11, [rbp+40+win32Globals] ; framebuffer ptr
  add  rax, r11                  ; адрес пикселя
  mov  [rax], r10d               ; записать цвет (32-bit)
```

#### clear(color)

```zenith
clear(0xFF101018)
```

Заливает весь framebuffer одним цветом через `rep stosd` — массовую запись:

```asm
  mov  edi, [rbp+40+win32Globals+40]  ; framebuffer ptr
  mov  ecx, [rbp+32+win32Globals]     ; width
  imul ecx, [rbp+36+win32Globals]     ; × height = общее число пикселей
  mov  eax, 0xFF101018                 ; цвет ARGB
  rep stosd                            ; записать EAX в [RDI], ECX раз
```

`rep stosd` (код `F3 AB`) — одна инструкция для массовой заливки памяти.
Это намного быстрее, чем цикл с `drawPixel`.

#### present()

```asm
  ; 1. BitBlt: копировать backbuffer на экран
  call GetDC(hwnd)
  mov  ecx, hdc                ; dst
  xor  edx, edx                ; x = 0
  xor  r8d, r8d                ; y = 0
  mov  r9d, width
  push height
  mov  [rsp+8], hdcMem         ; src DC
  push 0                        ; src x
  push 0                        ; src y
  push 0x00CC0020               ; SRCCOPY
  call BitBlt

  ; 2. Message pump (PeekMessageA loop)
  loop:
    PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)
    test eax, eax
    jz   done                   ; нет сообщений → выход
    TranslateMessage(&msg)
    DispatchMessageA(&msg)
    jmp  loop
  done:

  ; 3. Sleep(1) — освободить CPU
  mov  ecx, 1
  call Sleep
```

#### getKey(vk)

```zenith
if getKey(27) == 1     # ESC нажат?
```

```asm
  mov  ecx, 27                ; VK_ESCAPE = 27
  call GetAsyncKeyState       ; возвращает младшие 16 бит
  shr  eax, 15                ; бит 15 = 1 если клавиша зажата
  and  eax, 1                 ; маска: 0 или 1
```

**Популярные VK-коды:** 27=Esc, 32=Space, 13=Enter, 37-40=стрелки, 65-90=A-Z, 112-123=F1-F12.

#### processMessages()

Неблокирующая обработка сообщений. Возвращает 1 (продолжать) или 0 (WM_QUIT получен).

```asm
  PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)
  test eax, eax
  jz   return1                ; нет сообщений → продолжаем
  cmp  [msg+4], 0x12          ; WM_QUIT = 0x12
  je   return0                ; получен WM_QUIT → выходим
  TranslateMessage(&msg)
  DispatchMessageA(&msg)
return1:
  mov  eax, 1
  ret
return0:
  xor  eax, eax               ; return 0
  ret
```

#### WndProc — оконная процедура

Компилятор генерирует нативную x86_64 `WndProc` прямо в `.text` секции:

```asm
WndProc(hwnd, msg, wParam, lParam):
  cmp  edx, 2                  ; WM_DESTROY = 2?
  jne  .default
  xor  ecx, ecx                ; exit code = 0
  call PostQuitMessage          ; завершить message loop
  xor  eax, eax
  ret
.default:
  jmp  DefWindowProcA           ; tail-call к дефолтному обработчику
```

### Память — аллокаторы

#### Bump Allocator (64KB куча)

65536 байт статической памяти в секции `.data`:

```
.heapOffset:  qword 0          ; текущий offset (начинается с 0)
.heapArea:    rb 65536          ; сама область данных
```

**`alloc(size)`:**

```asm
  mov  rax, [rip + heapOffsetRVA]   ; текущий offset
  mov  rdx, rax
  add  rdx, rcx                     ; offset + size
  cmp  rdx, 65536                   ; проверка переполнения
  ja   fail                         ; > 64KB → вернуть 0
  mov  [rip + heapOffsetRVA], rdx   ; обновить offset
  lea  rax, [rip + heapAreaRVA]     ; базовый адрес кучи
  add  rax, rax                     ; + old offset = указатель
  ret
fail:
  xor  eax, eax                     ; return 0
  ret
```

**`free(ptr)`:** No-op. Bump-allocator не поддерживает освобождение отдельных блоков.

#### Arena Allocator

Обёртка над bump-allocator с header:

```
[capacity:8][used:8][данные...]
```

- `arenaAlloc`: `ptr = handle + 16 + used; used += size`
- `arenaReset`: `used = 0` (все указатели невалидны, память переиспользуется)

#### Pool Allocator

Фиксированные блоки:

```
[blockSize:8][count:8][nextIdx:8][данные...]
```

- `poolAlloc`: `ptr = handle + 24 + nextIdx * blockSize; nextIdx++`
- O(1) аллокация, нет фрагментации

#### Slot Allocator — поколенческий

ECS-стиль allocator с защитой от use-after-free.

**Handle:** `(generation << 32) | slotIndex` — 64-bit, старшие 32 бита = поколение.

**Header (32 байта):**
```
[maxCount:8][dataSize:8][freeHead:8][aliveCount:8]
```

**Слоты (начинаются с offset 32):**
```
Stride = 16 + dataSize
Каждый слот: [generation:8][nextFree:8][data: dataSize]
```

**`slotSpawn(slot)`:**
- Если `freeHead == -1`: используем `aliveCount` как индекс, bump generation
- Иначе: pop из free list, bump generation
- Handle = `(new_generation << 32) | index`

**`slotKill(slot, handle)`:**
- Извлечь index: `index = handle & 0xFFFFFFFF` (инструкция `mov eax, eax`)
- Инкрементировать generation в слоте → старый handle невалиден
- Добавить слот в free list (push в начало)
- Уменьшить aliveCount

### PE формат — структура .exe

Компилятор генерирует Windows PE32+ (x86_64) с нуля, без линкера.

```
Offset 0x00:  DOS Header (0x80 байт)
                e_magic = "MZ"
                e_lfanew = 0x80 (указатель на PE header)
              DOS Stub: "This program cannot be run in DOS mode."

Offset 0x80:  PE Signature ("PE\0\0")
              COFF Header:
                Machine = 0x8664 (AMD64)
                NumberOfSections = 3
                Characteristics = 0x0022
              Optional Header (PE32+):
                Magic = 0x020B
                ImageBase = 0x140000000
                SectionAlignment = 0x1000 (4KB)
                FileAlignment = 0x200 (512 байт)
                Subsystem = 3 (Console) или 2 (GUI)
                DllCharacteristics = 0x0160
                  (NX_COMPAT + DYNAMIC_BASE + HIGH_ENTROPY_VA)
                Stack: 1MB reserve, 4KB commit
                Heap: 1MB reserve, 4KB commit

Offset 0x400: .text секция (0x60000020 — code, exec, read)
                Entry point:
                  sub  rsp, 0x28
                  call main
                  mov  ecx, eax
                  call [rip + IAT_ExitProcess]
                + Machine code всех функций
                + WndProc (для GUI)

Offset 0xYY:  .rdata секция (0x40000040 — read-only)
                IMAGE_IMPORT_DESCRIPTOR[] (20 байт на DLL)
                DLL name strings
                Hint/Name table (hint + "FuncName\0")
                String pool (все строковые литералы)
                Class name "ZenithWnd" (для GUI)

Offset 0xZZ:  .data секция (0xC0000040 — read/write)
                ILT (Import Lookup Table)
                IAT (Import Address Table)
                heapOffset (8 байт, начальное значение 0)
                heapArea (65536 байт)
                win32Globals (56 байт, для GUI)
                Embedded DLL blobs (при --embed)
```

**Entry point:**

```asm
entryPoint:
  sub  rsp, 0x28           ; shadow space
  ; [если --embed: загрузить DLL из .data → %TEMP% → LoadLibrary]
  call main                 ; вызов main()
  add  rsp, 0x28
  mov  ecx, eax             ; exit code = результат main
  call [rip + IAT_ExitProcess]
```

### Оптимизатор — Dead Code Elimination

Перед генерацией кода оптимизатор удаляет недостижимый код:

1. Находит `main()` (или первую функцию)
2. Транзитивно обходит все вызовы → множество **reachable** функций
3. Удаляет unreachable пользовательские функции
4. Удаляет неиспользуемые `extern`-объявления
5. Удаляет неиспользуемые глобальные переменные
6. Печатает предупреждения в stderr

```
Warning: unused function 'helper'
Warning: unused extern function 'MessageBoxA'
```

Исходный `.z` файл **не модифицируется** — удаление происходит только в AST перед codegen.

---

## Встроенные функции

### Консольные (доступны в `app console` и `app gui`)

#### print(value)

```zenith
print("Hello, World!")   # печать строки
print(42)                # печать целого числа
```

Печатает строку или целое число в stdout. Автоматически добавляет `\r\n` в конце вывода.
Реализован через Win32 API: `GetStdHandle` + `WriteFile`.

#### pause()

```zenith
pause()                  # выведет "Press any key to continue . . ." и ждёт нажатия Enter
```

Выводит стандартное приглашение и блокируется до нажатия Enter (или любой клавиши).
Реализован через `GetStdHandle` + `WriteFile` + `ReadFile`.

#### sleep(seconds)

```zenith
sleep(2)                 # пауза на 2 секунды
sleep(0.5)               # (int only) пауза на N секунд
```

Приостанавливает выполнение на N секунд.
Реализован через Win32 `Sleep(ms)` (конвертация секунд -> миллисекунды).

---

## Встроенные типы и структуры

### Примитивные типы

| Тип | Описание | Размер | Доступность |
|-----|----------|--------|-------------|
| `int` | Целое число (64-bit signed) | 8 байт | всегда |
| `float` | Число с плавающей точкой (SSE) | 4 байта | всегда |
| `bool` | Булев тип (`true`/`false`) | 8 байт | всегда |
| `string` | Строка (указатель в .rdata) | 8 байт | всегда |
| `void` | Ничего | — | всегда |
| `vec2` | 2D-вектор (x, y: float) | 8 байт | только `app gui` |
| `vec3` | 3D-вектор (x, y, z: float) | 12 байт | только `app gui` |
| `color` | Цвет (r, g, b, a: float) | 16 байт | только `app gui` |

### Встроенные структуры (автоматически добавляются в `app gui`)

#### vec2

```zenith
var v: vec2
v.x = 1.0
v.y = 2.0
```

| Поле | Тип |
|------|-----|
| `x` | `float` |
| `y` | `float` |

#### vec3

```zenith
var v: vec3
v.x = 1.0
v.y = 2.0
v.z = 3.0
```

| Поле | Тип |
|------|-----|
| `x` | `float` |
| `y` | `float` |
| `z` | `float` |

#### color

```zenith
var c: color
c.r = 1.0
c.g = 0.0
c.b = 0.0
c.a = 1.0
```

| Поле | Тип |
|------|-----|
| `r` | `float` |
| `g` | `float` |
| `b` | `float` |
| `a` | `float` |

---

## 2D-графика (GUI)

> **Доступно только в режиме `app gui`.**

Компилятор включает встроенные функции для программного framebuffer через Win32 GDI.
Без внешних библиотек — только `user32.dll` + `gdi32.dll`.

### createWindow(w, h, title)

```zenith
createWindow(640, 480, "My Game")
```

Создаёт окно заданного размера. Инициализирует:
- Оконный класс (`RegisterClassExA`)
- HWND (`CreateWindowExA`)
- HDC (`GetDC`)
- Backbuffer (`CreateCompatibleDC` + `CreateDIBSection`)

Глобальное состояние (56 байт в `.data`): initFlag, hwnd, hdc, memDC, framebuffer ptr, width, height.
Вызов безопасен многократно — второй раз пропускает инициализацию.

### clear(color)

```zenith
clear(0xFFFF0000)   # залить красным
```

Заливает framebuffer 32-битным цветом `0xAARRGGBB` через `rep stosd`.

### drawPixel(x, y, color)

```zenith
drawPixel(100, 200, 0xFFFFFFFF)   # белый пиксель
```

Рисует пиксель по координатам. Формула: `framebuffer + (y * width + x) * 4`.

### present()

```zenith
present()
```

- `PeekMessageA` + `TranslateMessage` + `DispatchMessageA` (окно остаётся отзывчивым)
- `BitBlt` из backbuffer в HDC окна
- `Sleep(1)` для освобождения CPU

### getKey(vk)

```zenith
while getKey(32) == 0     # ждём пробел (VK_SPACE = 32)
    present()
end
```

Возвращает 1 если клавиша зажата (через `GetAsyncKeyState`), иначе 0.
Параметр `vk` — виртуальный код клавиши Windows (например, 27 = Esc, 32 = Space, 123 = F12).

### processMessages()

```zenith
while processMessages() != 0
    clear(0xFF000000)
    present()
end
```

Неблокирующий цикл обработки сообщений. Возвращает 1 если приложение активно, 0 при `WM_QUIT`.
Полезен для главного цикла GUI-приложений, где нет ожидания клавиш.

### closeWindow()

```zenith
closeWindow()
```

Разрушает окно через `DestroyWindow`.

### Полный пример GUI

```zenith
app gui

func fill(x: int, y: int, w: int, h: int, col: int)
    var cx: int = x
    while cx < x + w
        var cy: int = y
        while cy < y + h
            drawPixel(cx, cy, col)
            cy = cy + 1
        end
        cx = cx + 1
    end
end

func main()
    createWindow(320, 240, "Zenith Window")
    clear(0xFF606060)

    # Рисуем синий прямоугольник
    fill(80, 60, 160, 120, 0xFF3366CC)

    present()
    while getKey(27) == 0
        present()
    end
    closeWindow()
end
```

---

## Аллокаторы памяти

Zenith включает три встроенных аллокатора памяти, доступных в обоих режимах (`app console` и `app gui`).

### Bump Allocator

Простой bump-allocator для временных данных. Выделяет память последовательно из 64KB кучи в секции `.data`.

| Функция | Описание |
|---------|----------|
| `alloc(size) -> ptr` | Выделяет `size` байт. Возвращает указатель или 0 при переполнении. |
| `free(ptr) -> void` | No-op (bump-allocator не поддерживает освобождение отдельных блоков). |

```zenith
var p: int = alloc(64)    # выделить 64 байта
free(p)                    # no-op
```

### Arena Allocator

Bump-allocator для временных игровых объектов. Выделяет память последовательно, освобождает все разом.

**Внутренняя структура (header):**
```
[capacity:8][used:8][data...]
 ^ handle    +8     +16
```

| Функция | Описание |
|---------|----------|
| `arenaCreate(capacity) -> handle` | Выделяет блок `capacity+16` байт на куче, возвращает handle. |
| `arenaAlloc(arena, size) -> ptr` | O(1) bump: возвращает ptr, увеличивает used на size. |
| `arenaReset(arena) -> void` | Сбрасывает used=0, все указатели невалидны, переиспользует блок. |
| `arenaDestroy(arena) -> void` | No-op (память на куче, освобождается при завершении). |

```zenith
var arena: int = arenaCreate(4096)   # выделить блок 4096+16 байт
var p1: int = arenaAlloc(arena, 64)  # O(1) bump: ptr, used += 64
var p2: int = arenaAlloc(arena, 128) # следующий ptr = p1 + 64
arenaReset(arena)                    # сброс used=0
arenaDestroy(arena)                  # no-op
```

### Pool Allocator

Monotonic pool allocator — O(1) аллокатор для фиксированных блоков.
Идеален для пулов сущностей, projectiles, частиц.

**Внутренняя структура (header):**
```
[blockSize:8][count:8][nextIdx:8][data...]
 ^ handle     +8       +16        +24
```

| Функция | Описание |
|---------|----------|
| `poolCreate(blockSize, count) -> handle` | Создаёт пул: `count` блоков по `blockSize` байт. |
| `poolAlloc(pool) -> ptr` | O(1): следующий свободный блок (или 0 при переполнении). |
| `poolFree(pool, ptr) -> void` | No-op (используйте `poolReset` для освобождения всех). |
| `poolReset(pool) -> void` | Сбрасывает nextIdx=0, все блоки снова доступны. |
| `poolDestroy(pool) -> void` | No-op. |

```zenith
var pool: int = poolCreate(64, 100)  # 100 блоков по 64 байта
var p1: int = poolAlloc(pool)        # O(1): следующий блок
var p2: int = poolAlloc(pool)
poolReset(pool)                      # все блоки снова свободны
poolDestroy(pool)                    # no-op
```

> **Важно:** `poolFree(pool, ptr)` — no-op. Используйте `poolReset` для освобождения всех блоков сразу.

### Slot Allocator

Generational slot allocator для игровых объектов. Поддерживает безопасное удаление через поколения (generation) — после `slotKill` старый handle становится невалидным.

**Внутренняя структура (header):**
```
[maxCount:8][dataSize:8][freeHead:8][aliveCount:8]
^ handle      +8          +16         +24
```

Слоты начинаются по смещению 32 от handle. Структура слота:
```
[generation:8][nextFree:8][data:dataSize]
```

Размер слота (stride) = `16 + dataSize`.

Handle = `(generation << 32) | slotIndex` — поколение защищает от use-after-free.

| Функция | Описание |
|---------|----------|
| `slotCreate(maxCount, dataSize) -> handle` | Создаёт пул слотов. dataSize — размер данных каждого слота в байтах. |
| `slotSpawn(slot) -> handle` | Выделяет слот. Возвращает упакованный handle `(generation << 32) | index`. Возвращает 0 при переполнении. |
| `slotKill(slot, handle) -> void` | Освобождает слот: инкрементирует generation, добавляет в free list, уменьшает aliveCount. |
| `slotGetI64(slot, handle, byteOffset) -> int` | Читает 64-битное целое из данных слота по смещению. |
| `slotSetI64(slot, handle, byteOffset, value) -> void` | Записывает 64-битное целое в данные слота по смещению. |
| `slotGetF32(slot, handle, byteOffset) -> int` | Читает 32-битный float из данных слота (возвращает как int для SSE-интеропа). |
| `slotSetF32(slot, handle, byteOffset, value) -> void` | Записывает 32-битный float в данные слота (конвертация int -> float через cvtsi2ss). |
| `slotCount(slot) -> int` | Возвращает количество активных слотов (aliveCount). |
| `slotReset(slot) -> void` | Сбрасывает пул: freeHead=-1, aliveCount=0, все слоты свободны. |
| `slotDestroy(slot) -> void` | No-op. |

```zenith
app console

func main() -> int
    var s: int = slotCreate(4, 8)        # 4 слота, по 8 байт данных каждый

    var h1: int = slotSpawn(s)           # поколение=0, индекс=0 -> handle=0x000000000
    var h2: int = slotSpawn(s)           # поколение=0, индекс=1
    var h3: int = slotSpawn(s)           # поколение=0, индекс=2

    print(slotCount(s))                  # 3

    slotSetI64(s, h1, 0, 42)            # записать 42 в слот h1
    slotSetI64(s, h2, 0, 99)            # записать 99 в слот h2
    slotSetI64(s, h3, 0, 7)

    print(slotGetI64(s, h1, 0))          # 42
    print(slotGetI64(s, h2, 0))          # 99
    print(slotGetI64(s, h3, 0))          # 7

    slotKill(s, h2)                      # освободить h2 (generation инкрементируется)
    print(slotCount(s))                  # 2

    var h4: int = slotSpawn(s)           # переиспользует свободный слот (index=1, generation=1)
    print(slotCount(s))                  # 3

    slotSetI64(s, h4, 0, 55)
    print(slotGetI64(s, h4, 0))          # 55

    slotReset(s)                         # сброс всего пула
    print(slotCount(s))                  # 0

    slotDestroy(s)                       # no-op
    return 0
end
```

---

## Интероперабельность с C-библиотеками

### Механизм

Zenith вызывает C-функции через **прямой импорт DLL** на уровне PE-формата.
Никаких обёрток, FFI-фреймворков или дополнительных рантаймов.

### Шаг 1: объявление extern

```zenith
extern func InitWindow(width: int, height: int, title: string) -> void
extern func WindowShouldClose() -> bool
extern func BeginDrawing()
extern func EndDrawing()
extern func CloseWindow()
extern func DrawCircle(x: int, y: int, r: float, col: color)
```

### Шаг 2: компилятор добавляет DLL в Import Table

Для каждой `extern`-функции компилятор:

1. Находит DLL по имени функции (автоматический маппинг или через `@import`)
2. Добавляет DLL в IMAGE_IMPORT_DESCRIPTOR
3. Создаёт запись в Import Lookup Table + Hint/Name entry
4. Добавляет IAT-слот в .data секцию

### Шаг 3: вызов генерирует машинный код

```zenith
InitWindow(800, 600, "Zenith")
```

Превращается в:

```asm
mov  ecx, 800                  ; arg1
mov  edx, 600                  ; arg2
lea  r8, [rel "Zenith"]        ; arg3 (указатель на строку)
sub  rsp, 32                   ; shadow space
call [rip + IAT_InitWindow]    ; косвенный вызов через IAT
add  rsp, 32                   ; восстановить стек
```

### Как работает линковка DLL

PE-формат поддерживает динамическую линковку через **IAT (Import Address Table)**.
Загрузчик Windows при старте процесса:

1. Читает IMAGE_IMPORT_DESCRIPTOR из .rdata
2. Загружает указанные DLL (kernel32.dll, raylib.dll, ...)
3. Для каждой импортированной функции находит её адрес через GetProcAddress
4. Записывает адрес в IAT (секция .data)
5. Машинный код обращается к функциям через `call [rip + offset_to_IAT]`

### @import

Подключение внешних DLL:

```zenith
@import("user32.dll")
@import("my_engine.dll")
```

### Автоматический маппинг DLL

Компилятор автоматически определяет DLL по имени функции:

| Префикс / Имя | DLL |
|----------------|-----|
| `ExitProcess`, `GetStdHandle`, `WriteFile`, `ReadFile`, `HeapAlloc`, `HeapFree`, `GetProcessHeap`, `GetModuleHandleA`, `Sleep` | `kernel32.dll` |
| `CreateWindowExA`, `DefWindowProcA`, `RegisterClassExA`, `DestroyWindow`, `GetDC`, `ReleaseDC`, `PeekMessageA`, `TranslateMessage`, `DispatchMessageA`, `GetAsyncKeyState`, `PostQuitMessage`, `BeginPaint`, `EndPaint`, `GetMessageA`, `ShowWindow`, `UpdateWindow` | `user32.dll` |
| `CreateDIBSection`, `BitBlt`, `DeleteObject`, `SelectObject`, `DeleteDC`, `CreateCompatibleDC` | `gdi32.dll` |
| Остальные | `kernel32.dll` |

Явное указание DLL через `@import` переопределяет авто-маппинг.

---

## Архитектура компилятора

```
+--------------+
|  Source.z    |
+------+-------+
       |
       v
+--------------+
|   Lexer      |  <- токенизация: func, ident, number, +, -, ...
+------+-------+
       | tokens
       v
+--------------+
|   Parser     |  <- построение AST
+------+-------+
       | AST
       v
+--------------+
|  Optimizer   |  <- Dead Code Elimination
+------+-------+
       | AST (очищенный)
       v
+--------------+
|   Codegen    |  <- генерация x86_64 машинного кода + PE
+------+-------+
       | .exe
       v
+--------------+
|  game.exe    |
+--------------+
```

### 1. Лексер (Lexer) -- `src/lexer.cpp`

Разбивает исходный код на последовательность токенов (46 типов токенов).

**Ключевые слова (14):**

| Ключевое слово | Назначение |
|---------------|------------|
| `app` | Директива типа приложения |
| `func` | Объявление функции |
| `extern` | Внешняя (C) функция |
| `var` | Переменная |
| `if` / `else` | Условный оператор |
| `while` | Цикл |
| `return` | Возврат из функции |
| `struct` | Объявление структуры |
| `end` | Завершение блока |
| `true` / `false` | Булевы литералы |
| `import` | Импорт DLL (в `@import`) |
| `for` | Цикл счётчика |
| `let` | Алиас для `var` (одинаковая семантика) |

**Типы (8):** `int`, `float`, `bool`, `string`, `void`, `vec2`, `vec3`, `color`, `entity` (entity зарезервирован)

**Литералы:** числа (int, hex `0xFF`), числа с плавающей точкой (`3.14`, `1.0f`), строки (`"text"`)

**Операторы:** `+`, `-`, `*`, `/`, `=`, `==`, `!=`, `<`, `>`, `<=`, `>=`, `->`, `!`, `.`

**Пунктуация:** `(`, `)`, `[`, `]`, `{`, `}`, `,`, `:`, `@`

### 2. Парсер (Parser) -- `src/parser.cpp`

Строит абстрактное синтаксическое дерево (AST). 9 типов выражений, 6 типов операторов, 4 типа верхних узлов.

**Узлы AST:**

```
Program
+-- ImportDecl (dllName)              <- @import("dll.dll")
+-- StructDecl (name, fields[])       <- struct с полями
+-- FunctionDecl (name, params[], returnType, body, isExtern)
|   +-- Param (name, type)
|   +-- Block
|       +-- VarDecl (name, type, init, arraySize)    <- var arr: [N]type
|       +-- ReturnStmt (value)
|       +-- ExprStmt (expr)
|       +-- AssignStmt (name, memberPath[], indexExpr, value)
|       +-- IfStmt (condition, thenBlock, elseBlock)
|       +-- WhileStmt (condition, body)
+-- VarDecl (глобальные переменные)
```

**Типы выражений:**
- `NumberExpr` — целочисленный литерал
- `FloatExpr` — литерал с плавающей точкой
- `StringExpr` — строковый литерал
- `IdentExpr` — ссылка на переменную
- `MemberExpr` — доступ к полю (`obj.field`)
- `BinaryExpr` — бинарная операция (`left op right`)
- `UnaryExpr` — унарная операция (`!expr`)
- `ArrayAccessExpr` — индекс массива (`arr[i]`)
- `CallExpr` — вызов функции

**Приоритет операторов (от низкого к высокому):**
1. Логическое ИЛИ: `||`
2. Логическое И: `&&`
3. Сравнение: `==`, `!=`, `<`, `>`, `<=`, `>=`
4. Сложение: `+`, `-`
5. Умножение: `*`, `/`
6. Унарные: `-expr`, `!expr`
7. Первичные: числа, строки, идентификаторы, вызовы, доступ к полям, индексы

### 3. Генератор кода (Codegen) -- `src/codegen.cpp`

Сердце компилятора (~4000 строк).

**Проход 1 — сбор stringPool и IAT:**
- `collectStrings()` — обходит AST, собирает строковые литералы в stringPool
- `buildImportData()` — формирует Import Directory, IAT, string pool в секциях .rdata и .data

**Проход 2 — генерация машинного кода:**
Обходит AST и эмитит x86_64 инструкции в буфер.

**Проход 3 — fixup и PE:**
- `fixupSectionRVAs()` — корректирует RVA при несовпадении размеров секций
- `resolveFixups()` / `resolveJmpFixups()` — патчит относительные адреса
- `buildPE()` — записывает DOS Header, PE Signature, Sections, выравнивание

---

## Оптимизатор (Dead Code Elimination) -- `src/optimizer.cpp`

Оптимизатор выполняет **Dead Code Elimination** (удаление мёртвого кода) перед генерацией машинного кода.
Исходный файл `.z` **не модифицируется** — оптимизация применяется только к AST, поэтому `.exe` получается чище.

### Что удаляется

| Тип | Описание |
|-----|----------|
| **Неиспользуемые пользовательские функции** | Функции, недостижимые из `main` (или первой объявленной функции), удаляются вместе с телом |
| **Неиспользуемые extern-функции** | `extern`-объявления, которые не вызываются изReachable функций |
| **Неиспользуемые глобальные переменные** | Глобальные `var`, на которые нет ссылок в reachable-коде |

### Как работает

1. Находит точку входа (`main` или первую пользовательскую функцию)
2. Транзитивно обходит все вызовы функций — строит множество **reachable**
3. Собирает все вызовы `extern`-функций из reachable-кода — помечает используемые
4. Собирает все обращения к глобальным переменным из reachable-кода
5. Удаляет unreachable функции, неиспользуемые extern-объявления и неиспользуемые глобалы
6. Печатает предупреждения в stderr перед компиляцией

### Пример вывода

```
Warning: unused function 'helper'
Warning: unused extern function 'MessageBoxA'
Compiling: 3 functions, 1894 bytes
```

### Доступ к оптимизатору

```cpp
#include "optimizer.h"
Optimizer opt;
OptResult result = opt.optimize(program);
// result.warnings — вектор строк-предупреждений
// result.removedFunctions — количество удалённых функций
// result.removedGlobals — количество удалённых глобалов
```

---

## Генерация машинного кода x86_64

### Регистровый аллокатор

Компилятор использует 4 GP-регистра: **RAX (0), RCX (1), RDX (2), RBX (3)**
и 8 XMM-регистров: **XMM0-XMM7** для SSE float-операций.

Аллокатор (`allocReg`/`freeReg`) отслеживает занятые регистры битовой маской.
При исчерпании — fallback на стек.

### Пролог / эпилог функции

```asm
; Пролог
push  rbp
mov   rbp, rsp
sub   rsp, frame_size    ; кратно 16 для выравнивания

; Эпилог
add   rsp, frame_size
pop   rbp
ret
```

### Арифметика

```asm
; Сложение: a + b
add   eax, ecx

; Умножение: a * b
imul  ecx, eax

; Деление: a / b
cqo
idiv  rcx

; Сравнение: a < b
cmp   eax, ecx
mov   eax, 0
jge   end
mov   eax, 1
end:
```

### Float (SSE)

```asm
; Загрузка float
movss  xmm0, [rip + float_const]

; Арифметика
addss  xmm0, xmm1    ; сложение
subss  xmm0, xmm1    ; вычитание
mulss  xmm0, xmm1    ; умножение
divss  xmm0, xmm1    ; деление

; Сравнение
ucomiss  xmm0, xmm1
seta     al           ; > 
setb     al           ; <
sete     al           ; ==
setae    al           ; >=
setbe    al           ; <=
setne    al           ; !=

; Конвертация
cvtsi2ss  xmm0, eax  ; int -> float
cvtss2si  eax, xmm0  ; float -> int
```

### Управляющие конструкции

```asm
; if/else
; condition -> eax
test  eax, eax
jz    else_label
; then block...
jmp   end_label
else_label:
; else block...
end_label:

; while
loop_label:
; condition -> eax
test  eax, eax
jz    end_label
; body...
jmp   loop_label
end_label:
```

### Строки (String Pool)

Строковые литералы хранятся в `.rdata` и адресуются через RIP-relative LEA:

```asm
lea  rax, [rip + disp32]   ; rax -> строка в .rdata
```

### Вызов функции (Win64 Calling Convention)

```asm
sub  rsp, 32              ; shadow space
; arg1 -> ecx (RCX)
; arg2 -> edx (RDX)
; arg3 -> r8d (R8)
; arg4 -> r9d (R9)
call func                 ; относительный вызов
add  rsp, 32              ; восстановить стек
```

| Параметр | Регистр | Размер |
|----------|---------|--------|
| 1 | RCX | 64-bit |
| 2 | RDX | 64-bit |
| 3 | R8 | 64-bit |
| 4 | R9 | 64-bit |
| далее | стек | — |

### Выравнивание стека

Перед `call` RSP должен быть выровнен по 16 байт.
В прологе `push rbp` выравнивает его обратно.
`frameSize` округляется вверх до ближайшего кратного 16.

---

## Формат PE (.exe)

Генератор кода формирует валидный PE32+ Portable Executable вручную.

### Структура файла

```
File offset 0x000: IMAGE_DOS_HEADER (64 bytes)
  - e_magic = "MZ"
  - e_lfanew = 0x80

File offset 0x040: DOS stub -> padding нулями до 0x80

File offset 0x080: PE signature "PE\0\0" (4 bytes)

File offset 0x084: IMAGE_FILE_HEADER (20 bytes)
  - Machine = 0x8664 (AMD64)
  - NumberOfSections = 3
  - SizeOfOptionalHeader = 240 (0xF0)
  - Characteristics = 0x0022 (EXECUTABLE | LARGE_ADDRESS_AWARE)

File offset 0x098: IMAGE_OPTIONAL_HEADER64 (240 bytes)
  - Magic = 0x020B (PE32+)
  - ImageBase = 0x140000000
  - SubSystem = 2 (GUI) или 3 (Console)
  - DllCharacteristics = 0x0160 (NX_COMPAT | DYNAMIC_BASE | HIGH_ENTROPY_VA)
  - Stack: 1MB reserve, 4KB commit
  - Heap: 1MB reserve, 4KB commit

File offset 0x188: Section .text  (RVA 0x1000, CODE|EXECUTE|READ)
File offset 0x1B0: Section .rdata (RVA 0x2000, INITIALIZED_DATA|READ)
File offset 0x1D8: Section .data  (RVA 0x3000, INITIALIZED_DATA|READ|WRITE)

File offset 0x200: .text  — машинный код
                   .rdata — Import Directory, string pool
                   .data  — IAT, heap allocator area (64KB)
```

### Автоматические импорты

| DLL | Функции |
|-----|---------|
| `kernel32.dll` | `ExitProcess`, `GetStdHandle`, `WriteFile`, `ReadFile`, `HeapAlloc`, `HeapFree`, `GetProcessHeap`, `GetModuleHandleA`, `Sleep` |
| `user32.dll` (GUI) | `CreateWindowExA`, `DefWindowProcA`, `RegisterClassExA`, `DestroyWindow`, `GetDC`, `ReleaseDC`, `PeekMessageA`, `TranslateMessage`, `DispatchMessageA`, `GetAsyncKeyState`, `PostQuitMessage`, `BeginPaint`, `EndPaint`, `GetMessageA`, `ShowWindow`, `UpdateWindow` |
| `gdi32.dll` (GUI) | `CreateDIBSection`, `BitBlt`, `SelectObject`, `DeleteObject`, `DeleteDC`, `CreateCompatibleDC` |
| `d3d11.dll` (DX11) | `D3D11CreateDevice`, `D3D11CreateDeviceAndSwapChain` |
| `dxgi.dll` (DX11) | `CreateDXGIFactory` |
| `d3dcompiler_47.dll` (DX11) | `D3DCompileFromFile` |

---

## Примеры

> **Примечание:** Многие примеры в `examples/` содержат экспериментальный код.
> Свежие стабильные примеры: `main.z`, `print_test.z`, `button.z`, `slot_test.z`, `pause_test.z`, `elseif_test.z`, `neg_test.z`.

### main.z — базовый вызов функции

```zenith
app console

func add(a: int, b: int) -> int
    return a + b
end

func main() -> int
    var result: int = add(5, 3)
    return result
end
```

-> exit code 8

### print_test.z — вывод строки

```zenith
app console

func main() -> int
    print("Hello from Zenith!")
    return 0
end
```

-> output: `Hello from Zenith!`

### if_test.z — условный переход

```zenith
app console

func max(a: int, b: int) -> int
    if a > b
        return a
    else
        return b
    end
end

func main() -> int
    var result: int = max(7, 3)
    return result
end
```

-> exit code 7

### while_test.z — цикл

```zenith
app console

func sum_to(n: int) -> int
    var i: int = 0
    var total: int = 0
    while i < n
        i = i + 1
        total = total + i
    end
    return total
end

func main() -> int
    return sum_to(10)
end
```

-> exit code 55 (сумма 1+2+...+10)

### pause_test.z — sleep и pause

```zenith
app console

func main() -> int
    print("Hello!")
    sleep(2)
    print("Waited 2 seconds")
    pause()
    print("Done!")
    return 0
end
```

-> выводит "Hello!", ждёт 2 секунды, выводит "Waited 2 seconds", ждёт нажатия Enter, выводит "Done!"

### slot_test.z — slot allocator

```zenith
app console

func main() -> int
    var s: int = slotCreate(4, 8)

    var h1: int = slotSpawn(s)
    var h2: int = slotSpawn(s)
    var h3: int = slotSpawn(s)

    print(slotCount(s))        # 3

    slotSetI64(s, h1, 0, 42)
    slotSetI64(s, h2, 0, 99)
    slotSetI64(s, h3, 0, 7)

    print(slotGetI64(s, h1, 0))   # 42
    print(slotGetI64(s, h2, 0))   # 99
    print(slotGetI64(s, h3, 0))   # 7

    slotKill(s, h2)
    print(slotCount(s))        # 2

    var h4: int = slotSpawn(s)
    print(slotCount(s))        # 3

    slotSetI64(s, h4, 0, 55)
    print(slotGetI64(s, h4, 0))   # 55

    return 0
end
```

### button.z — GUI кнопка

```zenith
app gui

func fill(x: int, y: int, w: int, h: int, col: int)
    var cx: int = x
    while cx < x + w
        var cy: int = y
        while cy < y + h
            drawPixel(cx, cy, col)
            cy = cy + 1
        end
        cx = cx + 1
    end
end

func main()
    createWindow(320, 240, "Button Demo")

    var bg: int = 0xFF606060
    var blue: int = 0xFF3366CC
    var green: int = 0xFF33AA33
    var white: int = 0xFFFFFFFF

    var bx: int = 85
    var by: int = 80
    var bw: int = 150
    var bh: int = 60

    var pressed: int = 0
    var col: int = blue

    while getKey(27) == 0
        clear(bg)

        if getKey(32)
            pressed = 1
            col = green
        else
            if pressed == 1
                pressed = 2
            end
            if pressed == 2
                col = green
            else
                col = blue
            end
        end

        fill(bx, by, bw, bh, col)
        fill(bx, by, bw, 2, white)

        present()
    end

    closeWindow()
end
```

-> открывает окно 320x240, рисует кнопку. Пробел меняет цвет. Esc закрывает.

---

## Текущее состояние (v2.0.0)

### Реализовано (v2.0.0)

- **Переменные:** `var` с типом и инициализацией
- **Функции:** `func` с параметрами, return, void
- **Вызовы функций:** до 4 аргументов (Win64 calling convention, RCX/RDX/R8/R9)
- **extern / @import:** вызов C-функций из DLL через IAT
- **Арифметика:** `+`, `-`, `*`, `/` (int64)
- **Сравнения:** `==`, `!=`, `<`, `>`, `<=`, `>=` (-> 0/1)
- **if / else if / else:** полная кодогенерация с `jcc`, цепочки условий
- **while:** циклы с условным переходом
- **Строки:** строковые литералы, пул в `.rdata`, RIP-relative LEA, экранирование (`\n`, `\t`, `\r`, `\\`, `\"`, `\0`)
- **Hex-литералы:** `0xFFFF0000` в дополнение к десятичным числам
- **print:** встроенный вывод строк и чисел в консоль (через Win32 WriteFile)
- **pause:** встроенный приостанов с ожиданием нажатия клавиши (через Win32 ReadFile)
- **sleep:** встроенная пауза на N секунд (через Win32 Sleep)
- **Float (SSE):** числа с плавающей точкой, `+` `-` `*` `/`, сравнения через XMM0-XMM7
- **Структуры:** `struct` с полями, layout, доступ через `.`
- **Массивы:** `var arr: [N]type`, индексация через `arr[i]`
- **@import("dll.dll"):** добавление DLL в Import Table
- **`for` цикл:** счётчик с шагом, отрицательный шаг, полная кодогенерация
- **`let`:** алиас для `var` (одинаковая семантика)
- **`&&` / `||`:** short-circuit логические операторы
- **`!` (NOT):** унарный оператор через `setz`/`movzx`
- **Multi-file проекты:** `zenith build` собирает из нескольких `.z` файлов
- **DLL-компиляция:** `--lib` / `--libs`, модульная система `libs.dll`
- **Embedded DLL:** `--embed` встраивает DLL в .exe
- **DirectX 11:** `type dx` для GPU-рендеринга
- **`peek` / `poke`:** прямой доступ к памяти
- **Регистровый аллокатор:** RAX/RCX/RDX/RBX + XMM0-XMM7
- **Выравнивание стека:** frameSize кратен 16
- **Bump allocator:** `alloc(size)` / `free(ptr)` с проверкой границ 64KB кучи
- **Arena allocator:** `arenaCreate(cap)`, `arenaAlloc(arena, size)`, `arenaReset(arena)`, `arenaDestroy(arena)`
- **Pool allocator:** `poolCreate(blockSize, count)`, `poolAlloc(pool)`, `poolFree(pool, ptr)`, `poolReset(pool)`, `poolDestroy(pool)`
- **Slot allocator:** `slotCreate(maxCount, dataSize)`, `slotSpawn(slot)`, `slotKill(slot, handle)`, `slotGetI64`, `slotSetI64`, `slotGetF32`, `slotSetF32`, `slotCount(slot)`, `slotReset(slot)`, `slotDestroy(slot)`
- **2D-графика:** `createWindow`, `clear`, `drawPixel`, `present`, `getKey`, `processMessages`, `closeWindow` — программный framebuffer через Win32 GDI (только `app gui`)
- **Именованные цвета:** Red, Green, Blue, White, Black, Yellow, Cyan, Magenta, Gray (только `app gui`)
- **Встроенные типы:** `vec2`, `vec3`, `color` (только `app gui`)
- **Пропуск ошибок:** синхронизация парсера при ошибках (продолжение компиляции)
- **Логическое НЕ:** оператор `!` через `xor rax, 1`
- **PE32+:** ручная генерация без внешних зависимостей (NX_COMPAT, DYNAMIC_BASE, HIGH_ENTROPY_VA)
- **Автоматические импорты:** kernel32.dll (ExitProcess, GetStdHandle, WriteFile, ReadFile, Sleep, HeapAlloc, HeapFree, GetProcessHeap, GetModuleHandleA), user32.dll + gdi32.dll (GUI)

### Ограничения

- **Только Windows x64** — PE-формат привязан к Win64
- **Нет `+=`, `-=`** и других compound assignment операторов
- **Нет `switch`/`case**
- **Нет lambda/функционального программирования**

### Планы

- `switch`/`case`
- `+=`, `-=` и другие compound assignment операторы
- entity — компонентно-ориентированная модель
- Полная float-арифметика через SSE во всех контекстах
- LLVM backend для кроссплатформенности

---

## История исправлений

### v2.0.0 — Текущий релиз

- **`for` цикл** — счётчик с шагом, отрицательный шаг, полная кодогенерация
- **`let`** — алиас для `var` (одинаковая семантика)
- **`&&` / `||`** — short-circuit логические операторы
- **`!` (NOT)** — унарный логический оператор через `setz`/`movzx`
- **DLL-компиляция** — `--lib` / `--libs` флаги, сборка в `libs.dll` с модульной системой
- **Embedded DLL** — `--embed` встраивает DLL в .exe, извлекает при запуске
- **Multi-file проекты** — `zenith build` собирает из нескольких `.z` файлов
- **DirectX 11** — `type dx` для GPU-рендеринга через D3D11
- **`peek` / `poke`** — прямой доступ к памяти (для shared memory и отладки)

### v1.0.0 — Предыдущий стабильный релиз

Массовый поиск и исправление ~40 багов во всех модулях компилятора.

#### Codegen — исправления формата PE и кодогенерации

| # | Проблема | Симптом | Исправление |
|---|----------|---------|-------------|
| 1 | **DllMain `ret 12`** — x86 stdcall epilogue в x64 бинарнике | DllMain корректно работает только в x86, в x64 падает | Заменён на `ret` (0xC3) |
| 2 | **Embedded DLL RVAs** — `fixupSectionRVAs` не корректирует embedded DLL RVAs | Embedded DLL невалидны при изменении размера секций | Добавлена коррекция `embeddedFullPathRVA`, `embeddedHFileRVA`, `embeddedHModuleRVA`, `embeddedWrittenRVA` и всех per-DLL RVAs |
| 3 | **DLL COFF Characteristics** — неправильные флаги | `32BIT_MACHINE` (0x0200) установлен для x64, отсутствует `LARGE_ADDRESS_AWARE` | Заменено `0x2102` на `0x2022` (RELOCS_STRIPPED + EXECUTABLE_IMAGE + LARGE_ADDRESS_AWARE) |
| 4 | **`buildExportDir` перекрывает layout** — растёт rdata после финализации | Export Directory перезаписывает строковый пул | Добавлена проверка перекрытия и коррекция после `buildExportDir` |
| 5 | **`emitMovRegImm` — битый `movsxd`** | `movsxd` не принимает immediate операнды; отрицательные значения генерировали неверный код | Заменено на `mov r64, imm64` (REX.W + B8+rd + 8-byte immediate) |
| 6 | **Overlap detection — warning-only** | Наложение секций только предупреждало, но не останавливало компиляцию | Добавлен `throw std::runtime_error` для аварийного завершения при перекрытии |
| 7 | **`print` builtin — register leak** | `print()` не восстанавливал `regsUsed` после встроенной генерации | Добавлено `regsUsed = 0` после emit builtin |
| 8 | **Float variable load — неверный размер** | Float переменные загружались как 64-bit вместо 32-bit | Исправлен размер загрузки для float |
| 9 | **`emitFloatExpr` для IdentExpr/MemberExpr** | Загрузка float-значения как адреса → crash | Заменено на `movss xmm, [rbp+offset]` |
| 10 | **Register leak в `emitExpr`** | Function pointer refs и other expressions не освобождали регистры | Исправлена логика освобождения регистров |
| 11 | **`emitMovssXmm` — неправильный ModRM для xmm8-15** | XMM регистры 8-15 генерировали неверный код | Добавлены REX.R/X пропуски для xmm8-15 |
| 12 | **Float argument detection в calls** | Float аргументы не маршрутизировались в XMM регистры | Исправлена логика определения float аргументов |

#### Parser — исправления синтаксиса

| # | Проблема | Симптом | Исправление |
|---|----------|---------|-------------|
| 13 | **`else if` цепочки** — `parseIf()` не мог обработать | Парсер падал с "Expected 'end' after if" при `else if` | Переписан `parseIf()`: `else if` парсится рекурсивно через `parseIf()` обёрнутый в `Block` |
| 14 | **`advance()` возвращает не тот токен** — буферизация от `peek()` | В конце файла `advance()` возвращал невалидный токен | Переписан `advance()`: сохраняет токен перед инкрементом `pos` |
| 15 | **`peekNext()` возвращает тот же токен** | Рядом с `Eof` `peekNext()` возвращал последний реальный токен | Теперь возвращает `Token{Eof}` вместо `tokens.back()` |
| 16 | **`Ident.Dot` greedy intercept** | Точка после идентификатора перехватывала MemberExpr даже перед `=` | Добавлен 4-токенный look-ahead `Ident . Ident =` для различения присваивания и доступа к полю |
| 17 | **`arr[0].field`** — цепочка после индекса | Массив + индекс + точка не парсились | Добавлена обработка dot-chain после цикла индексации в `parsePrimary()` |
| 18 | **`parseStruct` — blank lines перед `{`** | Пустые строки перед `}` ломали парсинг структуры | `if(Newline) advance()` заменён на `while(Newline) advance()` |
| 19 | **Unary minus → BinaryExpr** | `-expr` генерировал `0 - expr` вместо `neg` | Исправлено: unary minus создаёт `UnaryExpr` |
| 20 | **`skipToSyncPoint()` — мёртвый код** | Недостижимый вызов в `consume()` | Удалён |

#### Lexer — исправления

| # | Проблема | Симптом | Исправление |
|---|----------|---------|-------------|
| 21 | **Column tracking off-by-one** — копится при `current--` | Позиция символа сбивалась после `peek()` + `current--` | Добавлен `col--` перед `current--` в каждом `scan*` методе |
| 22 | **`isdigit(c)`/`isalpha(c)` — UB** | Undefined Behavior при отрицательных char значениях | Добавлен `(unsigned char)` cast |
| 23 | **Hex `0x` без цифр** — молча парсился как 0 | `0x` не вызывал ошибку | Добавлена ошибка при отсутствии цифр после `0x` |
| 24 | **`all()` — пропуск Eof после Error** | Error токен не сопровождался Eof | Теперь `all()` добавляет Eof после Error |

#### Main — исправления CLI и путей

| # | Проблема | Симптом | Исправление |
|---|----------|---------|-------------|
| 25 | **Cyrillic path corruption** — `std::string` для путей | Кириллические пути ломали `readFile` и логирование | `sourceFiles` → `vector<fs::path>`, `compilerPath` → `fs::path`, `readFile` принимает `const fs::path&` |
| 26 | **DLL read — unchecked `tellg()`** | При ошибке чтения DLL — crash на `tellg()=-1` | Добавлена проверка `size <= 0` |
| 27 | **`-o` flag без имени файла** | `zenith a.z -o` молча игнорировал `-o` | Добавлено сообщение об ошибке |
| 28 | **`#` в именах директорий** | `#` в пути ломал парсинг `Workspace.zen` | Теперь удаляется только `#` перед которым пробел/таб |
| 29 | **Legacy mode — CWD вместо compiler dir** | Не определялся каталог компилятора | Используется `GetModuleFileNameW` для определения пути |
| 30 | **DLL read failure — silent data loss** | Пустая DLL не вызывала ошибку | Добавлена проверка `data.empty()` |
| 31 | **Multi-file — directives stripped** | `# [no_main]` и `# [library]` stripились только из первого файла | Сtripping применяется ко всем файлам |
| 32 | **`combinedIsLib` — `prog.isLibrary` не установлен** | Library flag не доходил до Program | Добавлено `prog.isLibrary = combinedIsLib` |
| 33 | **Empty arg edge case** | Пустой аргумент в `argv` → crash на `arg[0]` | Добавлена проверка `!arg.empty()` |
| 34 | **Non-deterministic file order** | Порядок файлов зависел от OS | Добавлена `std::sort` перед компиляцией |
| 35 | **No file info в ошибках multi-file** | Ошибки не показывали из какого файла | Добавлен `lineSourceFile` вектор и `findOriginalFile()` |

#### AST — исправления

| # | Проблема | Симптом | Исправление |
|---|----------|---------|-------------|
| 36 | **`NumberExpr::value` не инициализирован** | UB при использовании | Добавлен `= 0` default |
| 37 | **`FloatExpr::value` не инициализирован** | UB при использовании | Добавлен `= 0.0` default |

### v0.7.2 — Предыдущий релиз

#### Codegen — базовые исправления

| # | Проблема | Симптом | Исправление |
|---|----------|---------|-------------|
| 1 | **Stack alignment** — `frameSize` был `(locals + 8) % 16 == 0` | RSP не выровнен по 16 перед `call` | `frameSize % 16 == 0` — округление locals до ближайшего кратного 16 |
| 2 | **emitAdd dst=0, src=3** — дубликат условия | Dead code, при коллизии регистров неверный результат | Удалён битый вариант, оставлен `44 03 C0` |
| 3 | **emitSub — пропущены r8** — отсутствовали комбинации dst/src=3 | SUB с r8d падал в generic fallback с неверной ModRM | Добавлены все 7 комбинаций с REX.R/REX.B |
| 4 | **emitImul — пропущены r8** — отсутствовали комбинации dst/src=3 | IMUL с r8d падал в generic fallback | Добавлены все 7 комбинаций с REX.R/REX.B |
| 5 | **TEST в if/while** — мёртвый `emit8(0x85)` + неверная ModRM для r8 | Двойная эмиссия `test`; для r8 эмитился `test ebx,ebx` | Удалён первый emit8; r8: `45 85 C0` |
| 6 | **resolveFixups — двойное патченье jmp** | resolveFixups трактовал `targetPos` как raw-адрес | Удалён битый jmp-код из resolveFixups |
| 7 | **Bump allocator — нет bounds check** | При выделении >64KB — Access Violation | Добавлен `cmp rcx, 65536; ja fail` |
| 8 | **ILT/IAT 4-byte entries** | PE32+ требует 8-байтовые THUNK_DATA | `writeDW` -> `writeDQ` для ILT и IAT |
| 9 | **importDescSize** — не учтён zero terminator | Неверный размер Import Directory | `(importDescCount + 1) * 20` |
| 10 | **Double->float cast в FloatExpr** | Float literal всегда загружался как 0 | `float fval = (float)flt->value;` перед memcpy |
| 11 | **emitFloatExpr IdentExpr/MemberExpr** | Загрузка float-значения как адреса -> crash | Заменён на прямой `movss xmm, [rbp+offset]` |
| 12 | **Принудительное regsUsed=0** | Оба операнда сравнения получали один регистр | Убрано принудительное обнуление |
| 13 | **drawPixel framebuffer offset** | `mov r11, [rbx+16]` читал hdc вместо framebuffer | `[rbx+16]` -> `[rbx+40]` |
| 14 | **clear handler — мёртвый код** | `rep stosd` портил регистры, зависание | Полная перезапись: `push rdi` + `rep stosd` + `pop rdi` |
| 15 | **WNDCLASSEXA.lpfnWndProc** | `mov qword [mem], imm64` не поддерживается | `mov rax, imm64; mov [rsp+8], rax` |
| 16 | **DllCharacteristics: DYNAMIC_BASE без .reloc** | Загрузчик отклонял .exe | `0x0160` -> `0x0100` (NX_COMPAT + DYNAMIC_BASE + HIGH_ENTROPY_VA) |
| 17 | **Hex-литералы не парсились** | `0xFF` парсился как `0` + идентификатор `xFF` | Добавлен `isxdigit` loop в `scanNumber` |
| 18 | **Slot allocator: slotSetI64 порядок push** | Value push был после mov rbx, clobbered value | Порядок: push value, then mov slot handle |
| 19 | **Slot allocator: slotSpawn free-list fall-through** | Отсутствовал `emitJmp(doneLabel)` — free-list путь всегда возвращал 0 | Добавлен `emitJmp(doneLabel)` перед fullLabel |
| 20 | **ReadFile не в externFuncMap** | `pause()` падал с "ReadFile not found in externFuncMap" | Добавлен `dllFuncMap["kernel32.dll"].push_back("ReadFile")` |
| 21 | **stringOffsets OOB crash** | Builtin-функции добавляли строки в stringPool после построения stringOffsets | Предварительное добавление строки pause в stringPool в buildImportData |

---

## Структура проекта

```
zenith/
+-- build.bat              # Сборка компилятора
+-- zenith.exe             # Скомпилированный компилятор (v2.0.0)
+-- install.ps1            # PowerShell скрипт установки
+-- src/
|   +-- main.cpp           # Точка входа, аргументы CLI, сборка проектов
|   +-- lexer.h            # Заголовок лексера (TokenKind enum)
|   +-- lexer.cpp          # Токенизация (~400 строк)
|   +-- ast.h              # AST-узлы (TypeKind, Expr, Stmt, Decl)
|   +-- parser.h           # Заголовок парсера
|   +-- parser.cpp         # Построение AST (~1200 строк)
|   +-- optimizer.h        # Заголовок оптимизатора
|   +-- optimizer.cpp      # Dead Code Elimination (~230 строк)
|   +-- codegen.h          # Заголовок кодгена
|   +-- codegen.cpp        # Генерация x86_64 + PE (~4000 строк)
|   +-- codegen_gui.cpp    # GUI-кодогенерация (createWindow, present, и т.д.)
|   +-- codegen_sw.cpp     # Software rendering (GDI) builtins
|   +-- codegen_dx11.cpp   # DirectX 11 builtins
|   +-- codegen_pe.cpp     # PE-формат, экспорт, embedded DLL
|   +-- codegen_builtins.cpp # Встроенные функции (print, sleep, pause)
+-- examples/
|   +-- main.z             # add(5,3) -> 8
|   +-- if_test.z          # max(7,3) -> 7
|   +-- while_test.z       # sum_to(10) -> 55
|   +-- print_test.z       # print("Hello!")
|   +-- print_int.z        # print(42)
|   +-- alloc_test.z       # alloc(16) -> адрес кучи
|   +-- manual_mem.z       # alloc + free + if -> 42
|   +-- struct_simple.z    # struct с float полями
|   +-- struct_test.z      # struct + float -> 1
|   +-- float_test.z       # float арифметика
|   +-- float_cmp*.z       # сравнения float
|   +-- slot_test.z        # slot allocator demo
|   +-- slot_min.z         # минимальный slot test
|   +-- pause_test.z       # sleep + pause demo
|   +-- pause_only.z       # pause demo
|   +-- button.z           # GUI: интерактивная кнопка
|   +-- simple.z           # GUI: минимальное окно
|   +-- min_gui.z          # GUI: processMessages loop
|   +-- switch12.z         # GUI: переключатель F12
|   +-- calc.z             # 18 переменных, арифметика
|   +-- many_calls.z       # 16 вызовов функций
|   +-- many_prints.z      # 20 переменных + print
|   +-- many_vars.z        # 30 переменных
|   +-- ...                # и экспериментальные тесты
+-- libs/
|   +-- build_all.bat      # Сборка библиотек (MinGW)
|   +-- libs.dll           # Системная библиотека (thread, mutex, async, pool)
|   +-- bin/               # Скомпилированные DLL-модули
|   +-- thread/            # Исходники на C
|       +-- thread_core.{c,h}
|       +-- thread_mutex.{c,h}
|       +-- thread_async.{c,h}
|       +-- thread_pool.{c,h}
+-- документация/          # Документация языка (.zed)
|   +-- 00_язык_зенит.zed
|   +-- 01_типы_данных.zed
|   +-- ...                # 12 файлов
+-- installer.iss          # Inno Setup скрипт установщика
+-- README.md
```

---

## Дополнительные команды

```bash
# Сборка и запуск
build.bat

# Компиляция конкретного примера
zenith examples/main.z -o game.exe
zenith examples/button.z -o button.exe
zenith examples/slot_test.z -o slot_test.exe
zenith examples/pause_test.z -o pause_test.exe
```
