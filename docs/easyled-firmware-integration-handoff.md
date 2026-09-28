# Firmware de pixels — recursos entregues e alinhamento com a EasyLED

**Data:** 22/09/2026

**Destinatário:** equipe EasyLED — backend, contratos e integração de dispositivos

**Origem:** frente de firmware esp32libfun / esp_pixels / esp_pixels_fx / esp_pixels_composer

**Objetivo de entrega do produto:** 15/10/2026

Equipe EasyLED,

Já temos uma base de controle de pixels funcionando em hardware, com presets,
composição 1D/2D, duas saídas, Mirror e parâmetros customizados. Iniciamos também
uma sessão de conectividade Wi-Fi + MQTT/WSS, ainda separada da aplicação de pixels.

Precisamos alinhar essa implementação ao contrato e à API que vocês já possuem.
Encontramos o contrato de controle v1.0.0 marcado como canônico e uma OpenAPI
com operações implementadas. **Podem confirmar quais revisões são a referência
para o firmware, quais fluxos já estão disponíveis em desenvolvimento e fornecer
exemplos completos para a primeira integração?**

Este documento descreve o estado entregue e as diferenças que precisamos resolver
em conjunto. Não propõe substituir os contratos da plataforma nem alterar seu
código. O documento será encaminhado por Rodrigo; não foi publicado como issue.

## 1. Base de implementação e evidências

| Parte | Estado de referência |
|---|---|
| Aplicação de pixels | Checkpoint local `4d8c1ac`; aplicação e controles validados na WROOM |
| Composer | `d575dbe`, versão local v0.2.3 |
| Efeitos | `6a93967`, versão local v0.2.2 |
| Transporte de pixels | `5d7db6b`, biblioteca esp_pixels |
| Sessão de rede independente | Checkpoint local `500209a`; build e validação pontual no simulador |
| SDK | ESP-IDF 6.0; builds desta entrega com 6.0.2 |
| Bancada atual | ESP32-WROOM-32; WS2812 RGB/ordem GRB; somente GPIO16 com 50 pixels ativa; GPIO17/12 inativa |

São checkpoints locais; não presumir que tags ou branches remotos já contenham
essas entregas. A aplicação de pixels passou pela execução das suítes em hardware,
com zero falhas no último ciclo, e pelas confirmações visuais de Rodrigo. Os 100
presets originais foram restaurados e verificados após a bancada.

**Os 100 presets atualmente gravados são dados descartáveis de teste.** Sua
restauração fez parte do procedimento de bancada, não de um requisito de migração
para o produto. As 100 posições representam a capacidade local disponível; o
usuário definirá novos presets pelo frontend. Não é necessário transportar os
nomes ou o conteúdo desse catálogo de teste para a plataforma.

O 2D foi observado como canvas virtual 5×10 em uma fita reta. Ainda não foi
validado em um painel físico montado em matriz. A nova conectividade não foi
validada contra o broker EasyLED nem junto ao renderizador em hardware.

## 2. Recursos de pixels implementados

| Recurso | Comportamento disponível | Limite ou detalhe relevante para integração |
|---|---|---|
| Renderização local | Efeitos executados pelo dispositivo; Composer controla a tarefa de renderização | A aplicação atual não depende de nuvem; perda de rede com firmware integrado ainda precisa de bancada |
| Segmentos | Até 32 segmentos; regiões 1D e 2D; efeitos e parâmetros por segmento | Em 1D, intervalo `[start, stop)`; em 2D, retângulo `(x,y,w,h)` no canvas |
| Composição | Sobreposição ordenada; Replace, Add e Alpha; brilho global e por segmento; opacidade | Brilho, opacidade, velocidade e intensidade usam valores 0–255; o significado dos controles depende do efeito |
| Estado e edição | Ativar, pausar, desabilitar e reiniciar segmentos; pausa global; edição preparada antes da publicação | O controle AT distingue configuração em edição da configuração confirmada |
| Cores e paletas | Até três cores RGB nos parâmetros; paletas e seleção da fonte de cor quando suportadas | A plataforma deve consultar metadados do efeito, não apresentar todos os controles indiscriminadamente |
| Mirror e reverse | Espelhamento simétrico 1D, independente da inversão de direção | Mirror não é suportado em segmento 2D; funciona em comprimentos pares e ímpares |
| Parâmetros customizados | Três valores por segmento, cada um de 0–255, com labels e flags de suporte | Já possuem controle AT, persistência e efeito consumidor real: Stripes |
| Saídas físicas | Até duas saídas no adaptador/aplicação atual; GPIO, quantidade e ordem RGB configuráveis | Os segmentos usam um espaço lógico global e podem atravessar a divisão entre saídas |
| Topologia persistente | Configuração de saídas preparada, validada e salva | Entra em vigor no próximo reboot; não há troca dinâmica de topologia nesta aplicação |
| Canvas / ledmap | Motor com mapeamento real e segmentos 2D; suporta células sem pixel no ledmap | A interface AT/persistência da aplicação atual gera um único retângulo progressivo ou serpentino; não oferece editor de mapa arbitrário |
| Presets | Capacidade de 100 posições nomeadas; salvar, listar, carregar, substituir e excluir | O conteúdo atual é descartável. A interface local usa nomes de 1–31 bytes ASCII, sensíveis a maiúsculas; a associação futura aos presets criados no frontend ainda precisa ser definida |
| Recuperação | Cena confirmada e presets persistidos em NVS; recuperação após reboot | Restaura configuração; não persiste fase de animação nem framebuffer |

O limite de validação atual é de 2048 pixels por saída. **Esse valor não é uma
capacidade de desempenho comprovada com Wi-Fi/TLS:** a configuração atual da
bancada possui 50 pixels ativos. As duas saídas, totalizando 62 pixels, foram
validadas em uma entrega anterior, mas GPIO17/12 está inativa agora. O orçamento
depende de segmentos, buffers, efeitos e memória disponível.
O manifesto do produto deverá informar capacidades reais da configuração final.

O controle AT já permite operar esses recursos localmente, incluindo comandos
de segmentos/publicação, presets, saídas e mapa. Ele serve à bancada e ao
diagnóstico; não estamos propondo transportar strings AT como protocolo EasyLED.
A integração deve consumir as APIs C++ e preservar a semântica do contrato remoto.

### Catálogo de efeitos entregue

| ID estável | Efeito | Dimensão | Controles declarados pelo efeito |
|---|---|---|---|
| 0 | Solid | 1D e 2D | Cor principal |
| 1 | Sinelon | 1D | Velocidade, intensidade/rastro, paleta e três cores |
| 2 | Rainbow | 1D | Velocidade, intensidade/densidade e paleta |
| 3 | WipeOnce | 1D | Velocidade e três cores |
| 4 | XYGradient | 2D | Velocidade, intensidade/inclinação X/Y e paleta |
| 5 | Stripes | 1D | Duas cores; custom1 = largura da faixa, custom2 = largura do intervalo, custom3 = fase |

No Stripes, largura e intervalo são `valor + 1`; fase usa o valor bruto.
Brilho, blend e opacidade pertencem ao compositor e não aos controles do efeito.
A referência dos controles é `FxInfo` da biblioteca. IDs existentes não devem ser
renumerados. Uma superfície com altura 1 exige efeito compatível com 1D, inclusive
quando o segmento foi declarado pela API de geometria 2D.

**Próxima entrega solicitada à Claude:** Blink=6, Breathe=7 e Chase=8. Não fazem
parte do catálogo validado acima; aguardamos implementação, revisão e bancada.

### Persistência: o que existe e o que não deve ser confundido

A cena usa registros versionados, CRC32 e cópias A/B; o formato atual escreve
schema 4 e continua lendo schemas 1, 2 e 3. Presets passam por validação de
efeitos, geometria, topologia e mapa antes de aplicação. Um preset incompatível
é rejeitado sem substituir a cena ativa.

Cada registro de cena ocupa no máximo 1295 B, e cada registro de preset, 1348 B,
com os limites atuais. A partição de presets tem 320 KiB. Esses tamanhos são do
formato binário local, **não limites de tamanho de JSON da plataforma**.

A aplicação diferencia configuração aplicada de configuração salva: uma falha
na persistência posterior à aplicação é reportada como aplicada e não salva.
Esse comportamento local não equivale, por si só, à transação durável exigida
para `apply_config`. Pela regra provisória recebida, o adaptador de produto deve
emitir `command_applied` somente após persistência recuperável e ativação atômica;
uma falha de gravação gera `command_failed` e preserva/restaura o estado anterior.
A geração interna do journal também não é o `config_version` remoto.

## 3. Conectividade preparada

Implementamos um componente pertencente à aplicação, em exemplo independente:

- Reutilização do Wi-Fi STA existente, com política de reconexão progressiva.
- MQTT 3.1.1 sobre WSS, com verificação do certificado e hostname; espera por
  relógio plausível antes de iniciar TLS, com SNTP no exemplo.
- Nova inscrição no tópico de comandos após reconexão; prontidão condicionada
  à confirmação de inscrição QoS1 pelo broker.
- Montagem de mensagens fragmentadas antes de entregá-las à aplicação.
- Limite atual de 1024 bytes por mensagem e fila de duas mensagens completas.
  Fila cheia descarta a nova mensagem e incrementa diagnóstico; não é fila durável.
- Publicação por fila QoS1, com outbox limitado; enqueue não significa aplicação
  do comando nem confirmação pelo backend.
- Rejeição de entregas retained no caminho de comandos. Deduplicação por
  `command_id`, validade temporal e recuperação de resultados ainda serão integradas.
  A regra provisória de deduplicação já foi fornecida pela EasyLED: um comando
  terminal repetido não é reexecutado e republica seu resultado terminal original.

O build independente passou em ESP-IDF 6.0.2. A montagem de mensagens e a
fronteira offline do contrato foram executadas no simulador: parser limitado do
envelope, SHA-256 exato, serialização de eventos e interfaces de journal/staging.
**Ainda não estão entregues:** bootstrap/claim do produto, executor dos comandos,
download HTTPS de configuração, implementação concreta do staging durável,
acknowledgements de negócio, publicação do estado no formato EasyLED,
OTA/rollback e integração de rede com renderização em um único firmware.

A aplicação de bancada usa partição de aplicação única, sem slots OTA. O exemplo
de rede possui outra tabela de partições. A integração deverá decidir o layout
de produto; os presets de teste não impõem uma obrigação de migração. Para os
presets que os usuários criarem futuramente, será necessário definir a política
de persistência e recuperação. As tabelas atuais não são intercambiáveis.

## 4. O que encontramos na plataforma

Consulta somente leitura em 22/09/2026, `easyled-platform/main` no commit
`306fa44a4f4112c3518f5fa7dfa3ecb105c19c85`:

- O [contrato de controle v1.0.0](https://github.com/rodrigo-s-lange/easyled-platform/blob/306fa44a4f4112c3518f5fa7dfa3ecb105c19c85/docs/contracts/easyled-control-contract.md)
  está marcado como canônico. Define WSS, identidade MQTT pelo UUID do dispositivo,
  tópicos `state/cmd/event/telemetry`, envelope com `command_id`, classes
  `apply_config`, `activate_preset`, `reboot` e `ota`, eventos e idempotência.
- A [OpenAPI](https://github.com/rodrigo-s-lange/easyled-platform/blob/306fa44a4f4112c3518f5fa7dfa3ecb105c19c85/docs/openapi.yaml)
  declara documentar operações implementadas, incluindo autoria de ledmaps,
  segmentos, presets, versões de configuração e slots. O
  [router](https://github.com/rodrigo-s-lange/easyled-platform/blob/306fa44a4f4112c3518f5fa7dfa3ecb105c19c85/api/go-api/router.go)
  registra essas rotas. O README contém uma descrição mais antiga; não usamos
  essa descrição para concluir que essas operações ainda não existem.
- A [issue #48](https://github.com/rodrigo-s-lange/easyled-platform/issues/48)
  acompanha firmware de produto, bootstrap, MQTT, staging durável e autonomia.
  Ela referencia credenciais em #47 e entrega HTTPS em #60. Estar descrito na
  issue não comprova disponibilidade em um ambiente implantado.
- O [SegmentConfig consultado](https://github.com/rodrigo-s-lange/easyled-platform/blob/306fa44a4f4112c3518f5fa7dfa3ecb105c19c85/api/go-api/models/segment.go)
  contém paleta, cores, velocidade, intensidade, brilho, blend e opacidade.
  Ainda não declara mirror, reversed, custom[3] e color_source. Precisamos
  confirmar se há trabalho mais recente em outra branch/checkout para esses campos.

Essas referências orientam a integração, mas não demonstram que todos os endpoints
necessários ao dispositivo já estejam consolidados ou implantados. Por favor,
indiquem revisões posteriores e correções onde houver trabalho ainda não publicado.

### Retorno da equipe EasyLED — 22/09/2026

A equipe confirmou `main` em `306fa44a4f4112c3518f5fa7dfa3ecb105c19c85`
e o contrato de controle v1.0.0 como referências suficientes para implementar a
fronteira de transporte e a máquina local de comandos. Não há ainda integração
real ponta a ponta nem ambiente de desenvolvimento implantado comprovado.

Já existem autoria HTTP de ledmaps, segmentos, presets, versões de configuração
e slots; persistência de comandos/auditoria; autenticação do broker; e definição
de tópicos, envelopes, eventos, estado retido e rejeições mínimas. Continuam
pendentes claim/bootstrap e credenciais MQTT (#47), publicação/outbox (#56),
ingestão de estado/evento (#57), reconciliação (#58), timeout/retry (#59),
entrega HTTPS dos bytes (#60), manifesto de capacidades (#61/PR #66) e o firmware
de produto (#48).

Também foram confirmadas as divergências que precisam de contrato:

- Catálogo da plataforma ainda contém somente efeitos 0–2; deve receber 3–8 por
  nova migration, preservando todos os IDs existentes.
- `SegmentConfig` ainda não contém mirror, reverse por segmento, custom[3] nem
  color_source. `digital_out.reversed` representa orientação física da saída e
  não pode ser usado como reverse de segmento.
- O backend atualmente calcula checksum dos bytes produzidos por `json.Marshal`,
  mas mantém apenas JSONB. #60 deve preservar os bytes canônicos ou definir uma
  canonicalização formal reproduzível.
- O manifesto foi decidido na PR #66 como `event/capability_reported` no boot e
  a cada 60 minutos, validade de 24 horas e vínculo à versão do firmware. Essa
  decisão ainda não pertence ao `main`.
- A correção de config-version para relé associado a dimmer desabilitado está na
  PR #71 e também ainda não pertence ao `main`.

O alinhamento provisório é identificado por `FW-CONTRACT-ALIGN-2026-09-22`.
Rodrigo mantém as decisões finais; Codex planeja/revisa e registra evidências;
Claude implementa após autorização explícita; a frente de firmware apresenta
evidência no dispositivo.

## 5. Decisões recebidas e pontos ainda abertos

| Tema | Estado do alinhamento |
|---|---|
| Referência oficial | Respondido provisoriamente: `main@306fa44` e contrato v1.0.0. As fixtures não são canônicas e não há ambiente implantado comprovado. |
| Bootstrap e credenciais | Qual é o fluxo do dispositivo até obter tenant_id/device_id, credencial HTTPS e credencial MQTT? Endpoints, autenticação, expiração/rotação e exemplo de resposta. Não enviar segredos neste documento. |
| Ambiente de desenvolvimento | Como obter um dispositivo de teste e acesso autorizado? Qual WSS URI/path, CA e versão MQTT? Fornecer dados de acesso pelo canal apropriado. |
| Entrega de configuração | Endpoint HTTPS real, autenticação, formato completo do payload, limite de bytes, algoritmo/checksum e definição dos bytes exatos verificados. Quais são as regras de timeout, retry e URL expirada? |
| Comandos e resultados | Fixtures provisórias foram fornecidas. `command_applied` só ocorre após validação, checksum aplicável, commit durável recuperável, ativação no frame boundary e confirmação do estado ativo. Falha de persistência gera `command_failed` e preserva/restaura o estado anterior. |
| Duplicatas e reconexão | Para `command_id` já terminal, não revalidar, baixar, gravar ou reaplicar; republicar o resultado terminal persistido, preservando resultado, motivo e `event_at`. Continuam abertas capacidade/janela do journal, comandos obsoletos e reconciliação. |
| Presets | Regra provisória: localizar primeiro por `preset_id` + checksum; em ausência/divergência, baixar bytes autenticados por HTTPS, validar, gravar e só então ativar. Novo comando para preset já ativo é no-op bem-sucedido e completa seu próprio ciclo. Ainda faltam URL/checksum canônicos, política das 100 posições e enum de transição. O catálogo de bancada é descartável. |
| Campos e catálogo | Como representar mirror/reversed/custom/color_source, estado dos segmentos e ordem de composição? Como alinhar IDs 0–5 e depois 6–8 sem renumeração? Precisamos de defaults explícitos e metadados de controles. |
| Topologia e 2D | Qual schema definitivo de outputs/ledmap, incluindo buracos? Como coordenar alterações que exigem reboot? Nosso descritor AT retangular não é equivalente ao ledmap completo da plataforma. |
| Estado e capacidades | Fixture provisória de state foi fornecida. PR #66 decide provisoriamente `event/capability_reported` no boot e a cada 60 min, válido por 24 h e vinculado ao firmware; falta entrada em `main` e fechamento dos demais detalhes de QoS/retain/reconciliação. |
| Escopo e ownership | Onde ficará o repositório de firmware de produto e quem mantém essa integração? Como tratar recursos de dimmer/relé mencionados no preset EasyLED, ainda não integrados à aplicação de pixels aqui descrita? |
| OTA e armazenamento | Qual contrato vigente de artefatos, assinatura, rollback e staging? O contrato de controle e a issue #48 descrevem aspectos distintos; precisamos fixar a exigência de MVP antes de fechar partições e boot. |

## 6. Fixtures provisórias recebidas

O pacote `FW-CONTRACT-ALIGN-2026-09-22` inclui exemplos provisórios de
`apply_config`, `activate_preset`, resolução e payload de preset, eventos de
sucesso/rejeição/falha, duplicata terminal e estado após configuração/preset.
Ele serve somente a parser, hash, serialização e máquina local de comandos.
Aceitar campos JSON desconhecidos não dispensa rejeitar tipo ou faixa inválida
nos campos consumidos.

O payload `apply_config` original de 62 pixels permanece como teste offline. A
fixture de bancada declara uma única `digital_out`, slot 0, com 50 pixels e
`gpio: 16`. Esse campo resolve a fixture de bancada, mas continua provisório até
ser incorporado ao schema canônico da plataforma.

Hashes informados pela equipe:

| Fixture | SHA-256 dos bytes exatos, sem newline final | Estado local |
|---|---|---|
| Configuração original de 62 pixels | `fffcb1c1ba7949c752bd78651c07ed04576d72464d3e5663a20919801e27a543` | Verificado por Codex: 444 bytes, hash confere |
| Configuração de bancada GPIO16/50 | `b62868aef25369d8b00db8dbc6af34c7c82b060b5570b0b7b24859a7d5547c5a` | Verificado por Codex: 475 bytes, hash confere |
| Preset com `stop_px=50` | `c9be5621fa0109c6acbf4461a47191b80b3b6ebae2d50714d5c56172f7cc5174` | Verificado por Codex: 269 bytes, hash confere |

Rodrigo forneceu o conteúdo compacto pelo canal de trabalho. Os escapes `\_`
introduzidos pela formatação foram normalizados para `_`, e o SHA-256 foi calculado
sobre UTF-8 sem newline final. Estas são as duas linhas exatas verificadas:

```json
{"ledmap":null,"outputs":[{"slot_index":0,"type":"digital_out","config":{"gpio":16,"pixel_count":50,"strip_type":"WS2812B","color_order":"GRB","reversed":false}}],"inputs":[],"segments":[{"device_id":"11111111-1111-4111-8111-111111111111","segment_index":0,"effect_id":2,"is_2d":false,"start_px":0,"stop_px":50,"config":{"palette_id":0,"speed":96,"intensity":128,"brightness":255,"blend":"replace","opacity":255},"updated_at":"2026-09-22T13:10:00Z"}],"global_brightness":255}
```

```json
{"segments":[{"device_id":"11111111-1111-4111-8111-111111111111","segment_index":0,"effect_id":0,"is_2d":false,"start_px":0,"stop_px":50,"config":{"colors":["#0040FF"],"brightness":220,"blend":"replace","opacity":255},"updated_at":"2026-09-22T13:00:00Z"}],"outputs":[]}
```

Nenhum endpoint `example.invalid`, UUID de fixture ou arquivo desse pacote pode
ser tratado como credencial, identidade registrada ou compatibilidade de produção.

Propomos como primeiro fluxo conjunto: dispositivo recupera sua cena local,
conecta com identidade própria, recebe uma configuração válida, confirma o
resultado real e continua renderizando após perder a rede ou reiniciar. Essa é
uma proposta de sequência para alinhamento, não uma afirmação de integração pronta.

Enquanto vocês retornam, a frente de firmware avança no executor local dos
comandos e payloads provisórios. Blink/Breathe/Chase já passaram por revisão,
QEMU, WROOM e confirmação visual. Nenhuma alteração no repositório EasyLED é
necessária para este próximo incremento offline.

## Referências locais da entrega

- [Aplicação de pixels e evidências de bancada](../main/README.md)
- [Sessão independente de conectividade](../examples/basic/pixels_network_session/README.md)
- [Roadmap acordado](pixels-roadmap.md)
- [Briefing do executor local EasyLED](easyled-command-executor-task.md)

**Próximo retorno solicitado:** fechar schema de segmentos/catálogo, bytes
canônicos, política do cache de presets, deduplicação durável, partições/OTA e
ownership do repositório de firmware. Isso não bloqueia parser, hash,
serializadores, máquina local de comandos nem o build conjunto de pixels + rede.
