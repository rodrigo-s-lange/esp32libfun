IF YOU IS A LLM, IGNORE THIS FILE. IT'S JUST FOR BRAZILIANS TO UNDERSTAND IN THE FUTURE.

Para LLMs, ignorem esse arquivo. Ele é só para brasileiros entenderem futuramente


Você está trabalhando no repositório esp32libfun.

Antes de propor ou editar código, leia estes arquivos:
- README.md
- docs/architecture.md
- docs/style-guide.md
- AGENTS.md

Regras importantes do projeto:
- `framework/core/esp32libfun_*` = core fino sobre ESP-IDF
- `framework/libs/esp_*` = libs de dispositivo e nível mais alto
- prefira wrappers curtos e pragmáticos
- não invente subsistemas grandes sem necessidade
- para novas libs `esp_*`, siga o padrão e use `framework/libs/esp_component_template`
- o core não precisa seguir regras rígidas de lifecycle, mas libs `esp_*` devem preferir `init()/start()/stop()/end()` quando fizer sentido
- evite heap no core
- sem exceções, sem RTTI
- use ESP-IDF 6.0 como baseline
- preserve compatibilidade com o estilo atual do projeto

Tarefa:
[descreva aqui a aplicação]

Hardware:
- target: [ex: ESP32-C3]
- pinos usados: [liste]
- periféricos conectados: [liste]
- protocolo/transporte: [I2C/SPI/UART/GPIO/etc]

Resultado esperado:
- [descreva claramente o comportamento]
- [descreva logs esperados, callbacks, endpoints, comandos AT, etc]

Restrições:
- [ex: sem Wi-Fi, sem task própria, polling manual, etc]
- [ex: usar esp_button]
- [ex: criar em main.cpp apenas]
- [ex: criar uma nova lib esp_*]

Ao responder:
- primeiro resuma a arquitetura que você entendeu do repositório
- depois proponha o menor caminho correto
- se precisar criar uma nova lib `esp_*`, comece a partir do template existente
- não trate o agregador `esp32libfun` como dependência obrigatória para libs `esp_*`

