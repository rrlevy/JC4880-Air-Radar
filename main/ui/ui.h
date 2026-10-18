#pragma once

/* Arranque da interface e o protocolo da serial USB.
   O desenho esta em ui_radar.c, o cartao em ui_card.c, o toque em ui_input.c,
   o menu em ui_menu.c e a copia da tela em ui_shot.c. */
void ui_start(void);
/* Na serial USB: "shot" copia a tela.
   Sai @@SHOT W H, depois cada linha @@R yyyy, RGB565 e um byte de XOR, e @@END.
   "menu 0..4" abre Wi-Fi, Coordenadas, Filtros, Geral ou Tema. "radar" volta.
   "nm N" grava o alcance. "tema 0..3" grava Gol, Latam, Azul ou Generico.
   "mapa 0|1" e "rios 0|1" gravam o relevo. "avioes 0|1" e "heli 0|1" gravam o tipo.
   "tela" apaga ou acende o backlight.
   "lista" devolve @@LIST, uma linha @@A hex icao decimos foto por aeronave e @@ENDLIST.
   "sel HEX" prende essa aeronave. "solta" volta para a mais proxima. */
void ui_request_screenshot(void);
