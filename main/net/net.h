#pragma once

#include <stdbool.h>

void net_start(void);
/* Zoom out: busca ja e publica o resultado sem esperar a linha na vertical. */
void net_request_rush(void);
/* Tela apagada: nao consulta a API. O Wi-Fi e o relogio continuam. */
void net_set_dormant(bool on);
