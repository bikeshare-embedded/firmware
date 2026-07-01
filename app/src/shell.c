#include <errno.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/zbus/zbus.h>

#include "channels.h"
#include "config.h"
#include "state.h"

static int refresh_state_after_config(const struct shell *sh)
{
	int rc = bike_state_refresh_config();

	if (rc) {
		shell_error(sh, "Falha ao atualizar estado: %d", rc);
	}

	return rc;
}

static int cmd_bike_set_id(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	int rc = bike_config_set_id(argv[1]);

	if (rc) {
		shell_error(sh, "Erro ao salvar ID: %d", rc);
		return rc;
	}

	shell_print(sh, "ID configurado: %s", argv[1]);
	return refresh_state_after_config(sh);
}

static int cmd_bike_set_token(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	int rc = bike_config_set_device_token(argv[1]);

	if (rc) {
		shell_error(sh, "Erro ao salvar token: %d", rc);
		return rc;
	}

	shell_print(sh, "Token configurado.");
	return refresh_state_after_config(sh);
}

static int cmd_bike_set_mqtt_host(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	int rc = bike_config_set_mqtt_host(argv[1]);

	if (rc) {
		shell_error(sh, "Erro ao salvar MQTT host: %d", rc);
		return rc;
	}

	shell_print(sh, "MQTT host configurado: %s", argv[1]);
	return refresh_state_after_config(sh);
}

static int cmd_bike_set_mqtt_port(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	uint16_t port;
	int rc = bike_config_parse_mqtt_port(argv[1], &port);

	if (rc) {
		shell_error(sh, "Porta MQTT invalida: %s", argv[1]);
		return rc;
	}

	rc = bike_config_set_mqtt_port(port);
	if (rc) {
		shell_error(sh, "Erro ao salvar MQTT port: %d", rc);
		return rc;
	}

	shell_print(sh, "MQTT port configurado: %u", port);
	return refresh_state_after_config(sh);
}

static int cmd_bike_set_apn(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	int rc = bike_config_set_apn(argv[1]);

	if (rc) {
		shell_error(sh, "Erro ao salvar APN: %d", rc);
		return rc;
	}

	shell_print(sh, "APN configurado: %s", argv[1]);
	return refresh_state_after_config(sh);
}

static const char *show_string(const char *value)
{
	return value[0] ? value : "(nao definido)";
}

static int cmd_bike_get(const struct shell *sh, size_t argc, char **argv)
{
	const struct bike_config *cfg = bike_config_get();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "ID:        %s", show_string(cfg->id));
	shell_print(sh, "Token:     %s", show_string(cfg->device_token));
	shell_print(sh, "MQTT host: %s", show_string(cfg->mqtt_host));
	if (cfg->mqtt_port) {
		shell_print(sh, "MQTT port: %u", cfg->mqtt_port);
	} else {
		shell_print(sh, "MQTT port: (nao definido)");
	}
	shell_print(sh, "APN:       %s", show_string(cfg->apn));
	shell_print(sh, "Valida:    %s",
		    bike_config_is_valid(cfg) ? "sim" : "nao");
	return 0;
}

static int cmd_bike_state(const struct shell *sh, size_t argc, char **argv)
{
	const char *rental_id = bike_state_get_rental_id();

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "Estado: %s", bike_state_name(bike_state_get()));
	if (rental_id[0]) {
		shell_print(sh, "Rental ID: %s", rental_id);
	}
	return 0;
}

static int cmd_bike_sim_authorize(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);

	struct bike_backend_command_msg msg = {
		.type = BIKE_BACKEND_RENT_AUTHORIZE,
	};

	strncpy(msg.rental_id, argv[1], sizeof(msg.rental_id) - 1);
	msg.rental_id[sizeof(msg.rental_id) - 1] = '\0';

	int rc = zbus_chan_pub(&backend_command_chan, &msg, K_MSEC(100));

	if (rc) {
		shell_error(sh, "Falha ao publicar comando: %d", rc);
		return rc;
	}

	shell_print(sh, "RENT_AUTHORIZE publicado: %s", msg.rental_id);
	return 0;
}

static int cmd_bike_sim_cancel(const struct shell *sh, size_t argc, char **argv)
{
	struct bike_backend_command_msg msg = {
		.type = BIKE_BACKEND_RENT_CANCEL,
	};

	if (argc > 1) {
		strncpy(msg.rental_id, argv[1], sizeof(msg.rental_id) - 1);
		msg.rental_id[sizeof(msg.rental_id) - 1] = '\0';
	}

	int rc = zbus_chan_pub(&backend_command_chan, &msg, K_MSEC(100));

	if (rc) {
		shell_error(sh, "Falha ao publicar comando: %d", rc);
		return rc;
	}

	shell_print(sh, "RENT_CANCEL publicado");
	return 0;
}

static int cmd_bike_sim_button(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	struct bike_button_event_msg msg = {
		.uptime_ms = k_uptime_get(),
	};

	int rc = zbus_chan_pub(&button_event_chan, &msg, K_MSEC(100));

	if (rc) {
		shell_error(sh, "Falha ao publicar botao: %d", rc);
		return rc;
	}

	shell_print(sh, "Evento de botao publicado");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_bike_set,
	SHELL_CMD_ARG(id, NULL, "<bike_id> Identificador da bicicleta",
		      cmd_bike_set_id, 2, 0),
	SHELL_CMD_ARG(token, NULL, "<device_token> Token do dispositivo",
		      cmd_bike_set_token, 2, 0),
	SHELL_CMD_ARG(mqtt_host, NULL, "<host> Host do broker MQTT",
		      cmd_bike_set_mqtt_host, 2, 0),
	SHELL_CMD_ARG(mqtt_port, NULL, "<port> Porta do broker MQTT",
		      cmd_bike_set_mqtt_port, 2, 0),
	SHELL_CMD_ARG(apn, NULL, "<apn> APN da operadora",
		      cmd_bike_set_apn, 2, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_bike_sim,
	SHELL_CMD_ARG(authorize, NULL, "<rental_id> Simula RENT_AUTHORIZE",
		      cmd_bike_sim_authorize, 2, 0),
	SHELL_CMD_ARG(cancel, NULL, "[rental_id] Simula RENT_CANCEL",
		      cmd_bike_sim_cancel, 1, 1),
	SHELL_CMD(button, NULL, "Simula pressionamento do botao",
		  cmd_bike_sim_button),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_bike,
	SHELL_CMD(set, &sub_bike_set, "Configura parametros da bicicleta", NULL),
	SHELL_CMD(get, NULL, "Exibe configuracao atual", cmd_bike_get),
	SHELL_CMD(state, NULL, "Exibe estado atual", cmd_bike_state),
	SHELL_CMD(sim, &sub_bike_sim, "Simula eventos do backend e botao", NULL),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(bike, &sub_bike, "Gerenciamento da bicicleta", NULL);
