# JC4880 Air Radar

Firmware de um radar ADS-B em paisagem para a placa Guition JC4880P443C-I-W: ESP32-P4, ESP32-C6 no Wi-Fi e tela IPS de 4,3 polegadas, 480×800, com toque capacitivo. Na tela o mapa fica deitado, 800×480.

O centro, o alcance, a proa, o tema, os filtros e as redes Wi-Fi ficam na NVS da própria placa. O código não traz senha de rede. Sem coordenada gravada, o centro é o aeroporto de Congonhas (CGH).

## O que aparece na tela

- Anel com aviões em volta do centro escolhido. A proa é digitada: a placa não tem GPS nem bússola.
- Mapa do Brasil, rios principais (incluindo Tietê e Pinheiros) e aeroportos com voo regular. Em 250 NM, o alcance máximo, entram os grandes e os médios; mais perto, também os locais. No máximo 12 rótulos.
- Cartão da aeronave selecionada, com foto quando a API devolve uma.
- Temas Gol, LATAM, Azul e Genérico. O tema padrão, se nada estiver gravado, é o Genérico.
- Configuração pelo nome do Wi-Fi, no canto, ou arrastando o dedo para baixo.

Os pontos cardeais N, E, S e W ficam fora do anel e acompanham o norte verdadeiro. A cor dos rios é fixa e não segue o tema.

## O que este firmware não usa

Câmera, cartão SD, RS-485 e alto-falante estão na placa e ficam desligados. Os dois botões da direita não são GPIOs: um reinicia o chip e o outro é a tecla de energia. O botão da esquerda é o BOOT (GPIO35) e, se estiver preso no reset, entra no modo de gravação. Ele não abre a configuração.

## Compilar

ESP-IDF 5.5, alvo `esp32p4`. Na primeira vez o IDF baixa os componentes listados em `main/idf_component.yml` (`dependencies.lock` fixa as versões).

```bash
. $IDF_PATH/export.sh
cd jc4880_avioes_public
idf.py set-target esp32p4
idf.py build
```

`sdkconfig.defaults` guarda as opções do projeto. O `sdkconfig` gerado fica de fora do repositório.

## Gravar

Grave só o aplicativo, no endereço `0x20000`. Nesta placa o bootloader procura o aplicativo nesse endereço. Não apague a NVS e não grave o ESP32-C6: a partição `factory` é a única que este fluxo atualiza.

```bash
python -m esptool --chip esp32p4 -p PORTA -b 460800 \
  --before default-reset --after hard-reset \
  write-flash --flash-mode dio --flash-freq 40m --flash-size 16MB \
  0x20000 build/jc4880_avioes.bin
```

`idf.py flash` também grava o bootloader e a tabela de partições. Na placa que já está em uso, prefira o comando acima. O `partitions.csv` do projeto ainda marca a factory em `0x10000`; nesta placa o teste continua em `0x20000`.

## Serial

A mesma USB que grava o chip é o console, em 115200. Os comandos estão em `main/ui/ui.h`: `shot`, `menu 0..4`, `radar`, `nm N`, `tema 0..3`, `mapa 0|1`, `rios 0|1`, `tela`, `lista`, `sel HEX` e `solta`.

Depois de gravar uma compilação que muda a tela, este comando lê a serial e grava o conjunto em `telas/`:

```bash
python3 scripts/capturar_telas.py
```

A porta padrão é COM11. O script devolve o tema e o alcance que estavam na placa.

## Pastas

O mapa de cada arquivo está em [docs/ESTRUTURA.md](docs/ESTRUTURA.md). Em uma linha:

| Pasta | Papel |
| --- | --- |
| `main/app` | `app_main` |
| `main/cfg` | configuração na NVS |
| `main/model` | aviões, filtro, varredura, foto |
| `main/net` | ADS-B, rota, foto, relógio NTP |
| `main/wifi` | ESP32-C6 por SDIO |
| `main/ui` | tela LVGL, dividida por assunto |
| `main/geo` | aeroportos, contorno do Brasil, rios |
| `main/tz` | fuso horário no centro do radar |
| `main/gfx` | marcas e decodificador JPEG |
| `components/board_p4` | tela, toque e pinos da placa |
| `telas` | capturas da interface, não entram no binário |

## Licença

O código original está sob a [licença MIT](LICENSE). É a mais permissiva que convive com as peças redistribuídas aqui: o suporte da placa já é MIT, o driver de toque da Espressif permanece Apache 2.0, e o extrato do OpenStreetMap permanece na ODbL. A MIT não substitui essas licenças.

A MIT não cobre marca nem foto.

Os blobs em `main/gfx/logos` são logotipos das companhias, usados apenas para ilustrar a tela. As marcas continuam dos respectivos donos.

Os PNG em `telas/` mostram o software funcionando. Quando a captura inclui a foto de uma aeronave, essa foto foi obtida na hora pela API e ficou no quadro só para documentar a interface. O direito é do autor da foto. As capturas estão como saíram da placa.

### Origem do que não foi escrito aqui

| Peça | Onde está | Origem | Licença |
| --- | --- | --- | --- |
| Suporte da placa | `components/board_p4` | [ultramcu/guition-jc4880p4-bsp](https://github.com/ultramcu/guition-jc4880p4-bsp) | MIT |
| Toque GT911 | `components/esp_lcd_touch_gt911` | Espressif | Apache 2.0 |
| Decodificador JPEG | `main/gfx/jpgd` | jpgd, Rich Geldreich | Domínio público ou Apache 2.0, à escolha de quem reutiliza esse diretório |
| LVGL, JPEG da Espressif, Wi-Fi do C6 | baixados em `managed_components/` na compilação, fora do Git | `main/idf_component.yml` | A de cada componente. O LVGL 9 é MIT |
| Fonte da interface | embutida pelo LVGL | Montserrat, Julieta Ulanovsky | SIL Open Font License 1.1 |
| Aeroportos | `main/geo/airports.c` | [OurAirports](https://ourairports.com/data/), Brasil, serviço regular, código IATA, pista mais longa | Domínio público |
| Rios do país | `main/geo/rivers.c` | Natural Earth 1:10m, simplificados | Domínio público |
| Tietê e Pinheiros | `main/geo/rivers.c` | OpenStreetMap | ODbL. © colaboradores do OpenStreetMap |
| Contorno dos estados | `main/geo/brazil.c` | IBGE, pelo conjunto de polígonos do [click_that_hood](https://github.com/codeforamerica/click_that_hood), simplificado | Uso livre com citação do IBGE |
| Regras de fuso | `main/tz/tz_grid.bin`, `main/tz/tz_rule.c` | Strings POSIX no formato do banco de fusos da IANA. A grade de 0,5° é deste firmware; célula sem regra usa o fuso náutico, uma hora a cada 15° de longitude. As notas do projeto não nomeiam um arquivo de fronteiras | As strings seguem o banco da IANA, domínio público |
| Logotipos | `main/gfx/logos` | As companhias | Marca de terceiro. Só ilustração da tela |
| Foto no cartão e nas capturas | não fica no código; a API responde em tempo de execução | [planespotters](https://www.planespotters.net/photo/api); miniatura do airport-data se não houver foto lá | Direito do fotógrafo |

Em execução o firmware também consulta `opendata.adsb.fi` (aviões), `api.adsbdb.com` (rota) e um servidor NTP (hora). A resposta não entra no repositório. Vale o termo de cada serviço no momento da chamada.
