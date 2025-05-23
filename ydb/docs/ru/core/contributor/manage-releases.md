# Manage {{ ydb-short-name }} releases

На базе исходного кода из [репозитория {{ ydb-short-name }}](https://github.com/ydb-platform/ydb) разрабатываются два продукта с независимыми релизными циклами:

- [Сервер {{ ydb-short-name }}](#server)
- [Интерфейс командной строки {{ ydb-short-name }} (CLI)](#cli)

## Релизный цикл сервера {{ ydb-short-name }} {#server}

Данный документ описывает релизный цикл, начиная с мажорного релиза 25.1.

{% note info %}

Раньше версия сервера {{ ydb-short-name }} состояла из 3 чисел (например, v24.3.3), начиная с мажорной версии 25.1, добавлена четвертое число, которое обозначает номер патча (например, v25.1.1.3). Изменилась именование релизных веток и общая схема работы с ними - в мажорную релизную ветку можно мерджить новые фичи по согласованию, релизные теги создаются в минорных ветках, в которые можно мерджить только исправления ошибок. Подробнее про все изменения [тут](#release_branch_scheme).

{% endnote %}

Если у вас возникли вопросы по этому документу, обращайтесь к [команде {{ ydb-short-name }}](https://github.com/orgs/ydb-platform/teams/engineering).

### Номера и расписание релизов {#server-versioning}

Версия сервера {{ ydb-short-name }} состоит из четырех чисел, разделенных точками:

1. Две последние цифры календарного года релиза
2. Порядковый номер мажорного релиза в текущем году
3. Порядковый номер минорного релиза в этом мажорном релизе
4. Порядковый номер патча (релизного тега) в минорном релизе

Таким образом:

* Мажорная версия - это комбинация первых двух чисел (например, `25.1`)
* Минорная версия - это комбинация первых трёх чисел (например, `25.1.2`)
* Полная версия - это комбинация всех четырёх чисел (например, `23.1.2.1`).

В течение года обычно выпускается 4 мажорных релиза сервера {{ ydb-short-name }}: `YY.1` – первый, а `YY.4` - последний в году `YY`. Количество минорных релизов и патчей не является постоянным и может варьироваться от одного мажорного релиза к другому.

### Совместимость {#server-compatibility}

Совместимость версий {{ ydb-short-name }} - это гарантии, что кластер сможет работать, даже если на его узлах выполняются две смежные мажорные версии исполняемого файла сервера {{ ydb-short-name }}. Подробнее о процедуре обновления кластера вы можете прочитать в статье [Обновление {{ ydb-short-name }}](../devops/deployment-options/manual/update-executable.md).

Для обеспечения такой совместимости мажорные релизы выпускаются парами:

* В нечетных версиях добавляется новая функциональность, отключенная с помощью feature-флагов.
* В четных версиях эта функциональность включается по умолчанию.

Например, версия `25.1` поставляется с отключенной новой функциональностью, и может быть постепенно развернута на кластере, работающем под управлением `24.4`, без остановки работы кластера. Как только на всех узлах кластера будет запущена `25.1`, его можно будет далее обновить до `25.2`, чтобы использовать новые функциональные возможности.

### Релизные ветки и теги {#server-branches-tags}

#### Виды коммитов {#commit_types}

* **Фича**. К фичам относятся любые добавляющие новую функциональность или улучшающие существующую изменения, не связанные с исправлением ошибок.
* **Исправление ошибки**. Изменение, направленное на устранение конкретной ошибки.
* **Исправление критической ошибки**. Срочное исправление серьезной проблемы, которое требуется немедленно выкатить в продакшн. Без срочного исправления критических ошибок высока вероятность наступления серьезных негативных последствий.

#### Типы релизных веток {#release_branch_types}

* **Мажорная ветка** - ветка, в которой хранится исходный код соответствующей мажорной версии. Именуются `stable-XX-Y` (например, `stable-24-1` или `stable-25-1`). В эту ветку можно мерджить новые фичи и исправления ошибок.

* **Минорная ветка** - ветка, из которой собираются релизы {{ ydb-short-name }}. Именуется `stable-XX-Y-Z` (например, `stable-25-1-2`). Коммит, из которого собирается релиз помечается соответствующим релизным тегом, который имеет формат `XX.Y.Z.A`. Новая минорная ветка отводится от мажорной ветки после стабилизации предыдущего минорного релиза. В минорную ветку можно мерджить только исправления ошибок.

* **Хотфиксная ветка** - ветка для срочного исправления критических ошибок в конкретном релизном теге. Именуется `stable-XX-Y-Z-A-hotfix` (где `XX.Y.Z.A` - имя релизного тега), например, `stable-24-1-1-2-hotfix`. Такие ветки отводятся только от релизных тегов при необходимости сделать хотфикс. Из коммита хотфикса создается релизный тег, который именуется `stable-XX-Y-Z-A-hotfix-N` (где `stable-XX-Y-Z-A-hotfix` - имя хотфиксной ветки, N - порядковый номер хотфикса). При необходимости сделать хотфикс поверх ранее сделанного хотфикса, исправление коммитится в ту же хотфиксную ветку и из него создается новый релизный тег. В хотфиксные ветки можно мержить только исправления критических ошибок, которые требуется немедленно выкатить в продакшн.

#### Общая схема работы с ветками {#release_branch_scheme}

![Общая схема работы с ветками](_assets/major_release_branches.svg)

Цикл выпуска для нечетного мажорного релиза начинается с отведения от `main` мажорной ветки участником [команды {{ ydb-short-name }}](https://github.com/orgs/ydb-platform/teams/engineering). Название релизной ветки начинается с префикса `stable-`, за которым следует мажорная версия с точками, замененными на дефис (например, `stable-25-1`).

Цикл выпуска четного мажорного релиза (например, `stable-25-2`) начинается с отведения ветки от ветки предыдущего нечетного мажорного релиза.

От мажорной ветки отводится минорная ветка. Все выпуски минорных версий для нечетных и четных мажорных релизов, проходят цикл тестирования, в ходе которого выпускается ряд патчей. Каждый патч фиксируется назначением релизного тега с полным номером версии. Таким образом, в минорной ветке `stable-25-1-1` могут быть теги `25.1.1.1`, `25.1.1.2` и т.д. Как только тег успешно прошел необходимое тестирование, мы считаем его стабильным, регистрируем релиз на GitHub, добавляем на страницы [загрузки](../downloads/index.md#ydb-server) и в [список изменений](../changelog-server.md). Для мажорной версии может быть более одного стабильного релиза.

### Тестирование {#server-testing}

Каждая минорная версия проходит приемочное тестирование — комплексный процесс проверки соответствия требованиям качества. Тестирование включает в себя оценку производительности по стандартам (TPC-C, TPC-H), проверку совместимости между версиями и другие критически важные тесты. Последующее тестирование минорной версии включает выкладку на внутренние кластера и является итеративным. Каждая итерация начинается с назначения релизного тега на коммит минорной ветки. Например, тег 25.1.1.3 отмечает 3-ю итерацию тестирования для минорной ветки 25.1.1. Основываясь на выявленных проблемах, [релизная команда {{ ydb-short-name }}](https://github.com/orgs/ydb-platform/teams/release) решает, можно ли считать тег стабильным, или необходимо запустить новую итерацию тестирования.

{% include [corp_release_testing.md](_includes/corp_release_testing.md) %}

### Стабильный релиз {#server-stable}

Если итерация тестирования подтверждает качество минорного релиза, [релизная команда {{ ydb-short-name }}](https://github.com/orgs/ydb-platform/teams/release) готовит [перечень изменений](../changelog-server.md), и публикует релиз на страницах [Releases](https://github.com/ydb-platform/ydb/releases) на GitHub и [Загрузки](../downloads/index.md#ydb-server) в документации, объявляя его тем самым стабильным.

{% include [corp_release_stable.md](_includes/corp_release_stable.md) %}

## Цикл выпуска {{ ydb-short-name }} CLI (интерфейс командной строки) {#cli}

### Номера выпусков и расписание {#cli-versioning}

Версия {{ ydb-short-name }} CLI состоит из трех чисел, разделенных точкой:

1. Порядковый номер мажорного релиза (в настоящее время "2")
2. Порядковый номер минорного релиза в этом мажорном релизе
3. Порядковый номер патча (релизного тега) в минорном релизе

Например, версия `2.8.0` обозначате вторую мажорную, восьмую минорную версию, без патчей.

В отличие от сервера {{ ydb-short-name }}, для CLI не существует фиксированного расписания выпуска релизов. Новые минорные версии публикуются по мере готовности значимых улучшений или новой функциональности. При первоначальном выпуске минорной версии номер патча равен 0.

Процесс выпуска {{ ydb-short-name }} CLI значительно упрощен по сравнению с сервером {{ ydb-short-name }}, что позволяет выпускать релизы чаще.

### Релизные теги {#cli-tags}

Теги для {{ ydb-short-name }} CLI назначаются в транке (ветка `main`) участником [релизной команды {{ ydb-short-name }}](https://github.com/orgs/ydb-platform/teams/release) после запуска тестов для некоторой ревизии. Чтобы отличаться от тегов сервера {{ ydb-short-name }}, теги CLI {{ ydb-short-name }} содержат префикс `CLI_` перед номером версии, например [CLI_2.8.0](https://github.com/ydb-platform/ydb/tree/CLI_2.8.0).

{% include [corp_cli_tags.md](_includes/corp_cli_tags.md) %}

### Стабильный релиз {#cli-stable}

Чтобы объявить тег {{ ydb-short-name }} CLI стабильным, участник [релизной команды {{ ydb-short-name }}](https://github.com/orgs/ydb-platform/teams/release) готовит [перечень изменений](../changelog-cli.md), и публикует релиз на странице GitHub [Releases](https://github.com/ydb-platform/ydb/releases) и в разделе [Загрузки](../downloads/index.md#ydb-cli) документации.
