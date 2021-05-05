// SPDX-License-Identifier: GPL-2.0-only
/*
 *  src/gwbot/modules/003_admin/admin.c
 *
 *  Trnslate module for GNUWeebBot
 *
 *  Copyright (C) 2021  Ammar Faizi
 */


#include <stdio.h>
#include <unistd.h>
#include <json-c/json.h>
#include <gwbot/module.h>
#include <gwbot/lib/tg_api.h>
#include <gwbot/lib/string.h>

#include "header.h"

GWMOD_NAME_DEFINE(003_admin, "Admin module");

int GWMOD_STARTUP_DEFINE(003_admin, struct gwbot_state *state)
{
	return 0;
}


int GWMOD_SHUTDOWN_DEFINE(003_admin, struct gwbot_state *state)
{
	return 0;
}

typedef enum _mod_cmd_t {
	ADM_CMD_BAN	= (1u << 0u),
	ADM_CMD_UNBAN	= (1u << 1u),
	ADM_CMD_KICK	= (1u << 2u),
	ADM_CMD_WARN	= (1u << 3u),
	ADM_CMD_MUTE	= (1u << 4u),
	ADM_CMD_TMUTE	= (1u << 5u),
	ADM_CMD_UNMUTE	= (1u << 6u),
	ADM_CMD_PIN	= (1u << 7u),
	USR_CMD_REPORT	= (1u << 8u),
	USR_CMD_DELVOTE	= (1u << 9u),
} mod_cmd_t;

#define ADMIN_BITS			\
	(				\
		ADM_CMD_BAN	|	\
		ADM_CMD_UNBAN	|	\
		ADM_CMD_KICK	|	\
		ADM_CMD_WARN	|	\
		ADM_CMD_MUTE 	|	\
		ADM_CMD_TMUTE 	|	\
		ADM_CMD_UNMUTE	|	\
		ADM_CMD_PIN		\
	)


static inline bool is_ws(char c)
{
	return (c == ' ') || (c == '\n') || (c == '\t') || (c == '\r');
}

static bool check_is_sudoer(uint64_t user_id)
{
	/*
	 * TODO: Use binary search for large sudoers list.
	 */
	static uint64_t sudoers[] = {
		701123895ull,	// lappretard
		1213668471ull,	// nrudesu
		133862899ull,	// ryne4s,
		243692601ull,	// ammarfaizi2
		1472415329ull,	// mysticial
	};

	for (size_t i = 0; i < (sizeof(sudoers) / sizeof(*sudoers)); i++) {
		if (sudoers[i] == user_id)
			return true;
	}

	return false;
}


static int send_reply(const struct gwbot_thread *thread, struct tgev *evt,
		      const char *reply_text, uint64_t msg_id)
{
	int ret;
	tga_handle_t thandle;

	tga_screate(&thandle, thread->state->cfg->cred.token);
	ret = tga_send_msg(&thandle, &(const tga_send_msg_t){
		.chat_id          = tge_get_chat_id(evt),
		.reply_to_msg_id  = msg_id,
		.text             = reply_text,
		.parse_mode       = PARSE_MODE_HTML
	});
	tga_sdestroy(&thandle);

	if (ret)
		pr_err("tga_send_msg() on send_reply(): " PRERF, PREAR(-ret));

	return 0;
}


static int send_eperm(const struct gwbot_thread *thread, struct tgev *evt)
{
	static const char reply_text[] 
		= "You don't have permission to execute this command!";

	return send_reply(thread, evt, reply_text, tge_get_msg_id(evt));
}


static int do_ban(uint64_t chat_id, uint64_t user_id, time_t until_date,
		  bool revoke_msg)
{
	tga_handle_t thandle;

	tga_screate(&thandle, thread->state->cfg->cred.token);
	ret = tga_kick_chat_member(&thandle, &(const tga_kick_cm_t){
		.chat_id = tge_get_chat_id(evt),
		.user_id = target_uid,
		.until_date = 0,
		.revoke_msg = false
	});
}


static int exec_adm_cmd_ban(const struct gwbot_thread *thread, struct tgev *evt,
			    uint64_t target_uid, const char *reason)
{
	int ret;
	char reply_text[1024];
	tga_handle_t thandle;
	uint64_t reply_to_msg_id;
	struct tgev *reply_to = tge_get_reply_to(evt);
	json_object *json_obj;

	tga_screate(&thandle, thread->state->cfg->cred.token);
	ret = tga_kick_chat_member(&thandle, &(const tga_kick_cm_t){
		.chat_id = tge_get_chat_id(evt),
		.user_id = target_uid,
		.until_date = 0,
		.revoke_msg = false
	});

	
	if (ret) {
		pr_err("tga_kick_chat_member() on exec_adm_cmd_ban(): " PRERF,
		       PREAR(-ret));
		snprintf(reply_text, sizeof(reply_text),
			 "Error: tga_kick_chat_member(): " PRERF, PREAR(-ret));
		goto out;
	} else {
		const char *json_res = tge_get_res_body(&thandle);
		json_object *res;

		printf("res = %s\n", json_res);

		json_obj = json_tokener_parse(json_res);
		if (json_obj == NULL) {
			snprintf(reply_text, sizeof(reply_text),
				 "Error: Cannot parse JSON response from API");
			goto out;
		}

		if (!json_object_object_get_ex(json_obj, "ok", &res) || !res) {
			snprintf(reply_text, sizeof(reply_text),
				 "Cannot find \"ok\" key from JSON API");
			goto out;
		}


		if (json_object_get_boolean(res))
			/*
			 *
			 * Successful ban!
			 *
			 */
			goto out_ban_ok;


		if (!json_object_object_get_ex(json_obj, "description", &res)) {
			snprintf(reply_text, sizeof(reply_text),
				 "Error: Cannot parse JSON response from API");
			goto out;
		}
	}

out_ban_ok:
	if (reply_to) {
		size_t tmp, pos = 0, space = sizeof(reply_text);
		const struct tgevi_from *fr = tge_get_from(reply_to);


		tmp = (size_t)snprintf(reply_text, space,
				       "<a href=\"tg://user?id=%" PRIu64 "\">",
				       target_uid);
		pos   += tmp;
		space -= tmp;

		tmp = htmlspecialchars(
			reply_text + pos,
			space,
			fr->first_name,
			strnlen(fr->first_name, 0xfful)
		);

		pos   += tmp - 1;
		space -= tmp;

		if (fr->last_name) {
			reply_text[pos++] = ' ';
			space--;

			tmp = htmlspecialchars(
				reply_text + pos,
				space,
				fr->last_name,
				strnlen(fr->first_name, 0xfful)
			);
			pos   += tmp - 1;
			space -= tmp;
		}

		memcpy(reply_text + pos, "</a> has been banned!\0", 22);

		reply_to_msg_id = tge_get_msg_id(reply_to);

		printf("rep = \"%s\"\n", reply_text);
	} else {
		reply_to_msg_id = tge_get_msg_id(evt);
	}


out:
	tga_sdestroy(&thandle);
	return send_reply(thread, evt, reply_text, reply_to_msg_id);
}


int GWMOD_ENTRY_DEFINE(003_admin, const struct gwbot_thread *thread,
				     struct tgev *evt)
{
	char c;
	mod_cmd_t cmd;
	int ret = -ECANCELED;
	struct tgev *reply_to;
	uint64_t user_id, target_uid;
	const char *tx = tge_get_text(evt), *reason = NULL;

	if (tx == NULL)
		goto out;

	c = *tx++;
	if ((c != '!') && (c != '/') && (c != '.') && (c != '~'))
		goto out;


	c =
	(!strncmp("ban",     tx, 3) && (tx += 3) && (cmd = ADM_CMD_BAN))     ||
	(!strncmp("unban",   tx, 5) && (tx += 5) && (cmd = ADM_CMD_UNBAN))   ||
	(!strncmp("kick",    tx, 4) && (tx += 4) && (cmd = ADM_CMD_KICK))    ||
	(!strncmp("warn",    tx, 4) && (tx += 4) && (cmd = ADM_CMD_WARN))    ||
	(!strncmp("mute",    tx, 4) && (tx += 4) && (cmd = ADM_CMD_MUTE))    ||
	(!strncmp("tmute",   tx, 5) && (tx += 5) && (cmd = ADM_CMD_TMUTE))   ||
	(!strncmp("unmute",  tx, 6) && (tx += 6) && (cmd = ADM_CMD_UNMUTE))  ||
	(!strncmp("pin",     tx, 3) && (tx += 3) && (cmd = ADM_CMD_PIN))     ||
	(!strncmp("report",  tx, 6) && (tx += 6) && (cmd = USR_CMD_REPORT))  ||
	(!strncmp("delvote", tx, 7) && (tx += 7) && (cmd = USR_CMD_DELVOTE));

	if (!c)
		goto out;


	reply_to = tge_get_reply_to(evt);
	user_id  = tge_get_user_id(evt);

	c = *tx;
	if (c == '\0') {

		if (!reply_to)
			/*
			 * TODO: Send argument required, or reply required.
			 */
			goto out;


		goto run_module;
	}


	if (!is_ws(c))
		goto out;


	/*
	 * Skip white space.
	 */
	while (is_ws(*tx))
		tx++;

	reason = tx;

run_module:
	if (reply_to == NULL) {
		if (reason == NULL) {
			/*
			 * TODO: Parse the reason, it may contain
			 *       username, user_id, etc.
			 */
			goto out;
		}
	} else {
		target_uid = tge_get_user_id(reply_to);
	}


	if (cmd & ADMIN_BITS) {
		/*
		 * This command requires administrator privilege
		 */


		if (!check_is_sudoer(user_id)) {
			/*
			 * TODO: Send permission denied
			 * TODO: Group admin check
			 */
			return send_eperm(thread, evt);
		}

	}


	switch (cmd) {
	case ADM_CMD_BAN:
		return exec_adm_cmd_ban(thread, evt, target_uid, reason);
	case ADM_CMD_UNBAN:
		break;
	case ADM_CMD_KICK:
		break;
	case ADM_CMD_WARN:
		break;
	case ADM_CMD_MUTE:
		break;
	case ADM_CMD_TMUTE:
		break;
	case ADM_CMD_UNMUTE:
		break;
	case ADM_CMD_PIN:
		break;
	case USR_CMD_REPORT:
		break;
	case USR_CMD_DELVOTE:
		break;
	}

	printf("reason = %s\n", reason);
	printf("cmd = %u\n", cmd);
out:
	return ret;
}