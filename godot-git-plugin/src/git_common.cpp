#include <git_api.h>
#include <git_common.h>

void check_git2_errors(int error, const char *message, const char *extra) {
	const git_error *lg2err;
	const char *lg2msg = "", *lg2spacer = "";

	if (!error) {
		return;
	}

	if ((lg2err = git_error_last()) != NULL && lg2err->message != NULL) {
		lg2msg = lg2err->message;
		lg2spacer = " - ";
	}

	if (extra) {
		printf("Git API: %s '%s' [%d]%s%s\n", message, extra, error, lg2spacer, lg2msg);
	} else {
		printf("Git API: %s [%d]%s%s\n", message, error, lg2spacer, lg2msg);
	}
}

extern "C" int progress_cb(const char *str, int len, void *data) {
	(void)data;

	char *progress_str = new char[len + 1];
	memcpy(progress_str, str, len);
	progress_str[len] = '\0';
	godot::Godot::print("remote: " + godot::String(progress_str).strip_edges());
	delete[] progress_str;

	return 0;
}

extern "C" int update_cb(const char *refname, const git_oid *a, const git_oid *b, void *data) {
	constexpr int short_commit_length = 8;
	char a_str[short_commit_length + 1];
	char b_str[short_commit_length + 1];
	(void)data;

	git_oid_tostr(b_str, sizeof(b_str), b);
	if (git_oid_is_zero(a)) {
		godot::Godot::print("* [new] " + godot::String(b_str) + " " + godot::String(refname));
	} else {
		git_oid_tostr(a_str, sizeof(a_str), a);
		godot::Godot::print("[updated] " + godot::String(a_str) + "..." + godot::String(b_str) + " " + godot::String(refname));
	}

	return 0;
}

extern "C" int transfer_progress_cb(const git_indexer_progress *stats, void *payload) {
	(void)payload;

	if (stats->received_objects == stats->total_objects) {
		godot::Godot::print("Resolving deltas " + godot::String::num_int64(stats->indexed_deltas) + "/" + godot::String::num_int64(stats->total_deltas));
	} else if (stats->total_objects > 0) {
		godot::Godot::print("Received " + godot::String::num_int64(stats->received_objects) + "/" + godot::String::num_int64(stats->total_objects) + " objects (" + godot::String::num_int64(stats->indexed_objects) + ") in " + godot::String::num_int64(static_cast<int64_t>(stats->received_bytes)) + " bytes");
	}
	return 0;
}

extern "C" int fetchhead_foreach_cb(const char *ref_name, const char *remote_url, const git_oid *oid, unsigned int is_merge, void *payload) {
	if (is_merge) {
		git_oid_cpy((git_oid *)payload, oid);
	}
	return 0;
}

extern "C" int push_transfer_progress_cb(unsigned int current, unsigned int total, size_t bytes, void *payload) {
	int64_t progress = 100;

	if (total != 0) {
		progress = (static_cast<int64_t>(current) * 100) / total;
	}

	godot::Godot::print("Writing Objects: " +
			godot::String::num_int64(progress) + "% (" +
			godot::String::num_int64((int64_t)current) + "/" + godot::String::num_int64((int64_t)total) + "), " + godot::String::num_int64((int64_t)bytes) + " bytes, done.");
	return 0;
}

extern "C" int push_update_reference_cb(const char *refname, const char *status, void *data) {
	// Per libgit2's git_push_update_reference_cb doc: status is non-NULL
	// only when the remote rejected the update.
	if (status != nullptr) {
		godot::Godot::print_error("[rejected] " + godot::String(refname) + ": " + godot::String(status), __func__, __FILE__, __LINE__);
	} else {
		godot::Godot::print("[updated] " + godot::String(refname));
	}
	return 0;
}

extern "C" int credentials_cb(git_credential **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload) {
	Credentials *creds = (Credentials *)payload;

	godot::String proper_username = username_from_url ? godot::String(username_from_url) : creds->username;

	if (allowed_types & GIT_CREDENTIAL_USERPASS_PLAINTEXT) {
		return git_credential_userpass_plaintext_new(out, proper_username.alloc_c_string(), creds->password.alloc_c_string());
	}

	if (allowed_types & GIT_CREDENTIAL_SSH_KEY) {
		return git_credential_ssh_key_new(out,
				proper_username.alloc_c_string(),
				creds->ssh_public_key_path.alloc_c_string(),
				creds->ssh_private_key_path.alloc_c_string(),
				creds->ssh_passphrase.alloc_c_string());
	}

	if (allowed_types & GIT_CREDENTIAL_USERNAME) {
		return git_credential_username_new(out, proper_username.alloc_c_string());
	}

	return GIT_EUSER;
}

extern "C" int diff_line_callback_function(const git_diff_delta *delta, const git_diff_hunk *hunk, const git_diff_line *line, void *payload) {
	// First we NULL terminate the line text incoming
	char *content = new char[line->content_len + 1];
	memcpy(content, line->content, line->content_len);
	static int i = 0;
	content[line->content_len] = '\0';

	godot::String prefix = "";
	switch (line->origin) {
		case GIT_DIFF_LINE_DEL_EOFNL:
		case GIT_DIFF_LINE_DELETION:
			prefix = "-";
			break;

		case GIT_DIFF_LINE_ADD_EOFNL:
		case GIT_DIFF_LINE_ADDITION:
			prefix = "+";
			break;
	}

	godot::String content_str = content;

	godot::Dictionary result;
	result["content"] = prefix + content_str;
	result["status"] = prefix;
	result["new_line_number"] = line->new_lineno;
	result["line_count"] = line->num_lines;
	result["old_line_number"] = line->old_lineno;
	result["offset"] = line->content_offset;

	godot::GitAPI::get_singleton()->diff_contents.push_back(result);

	return 0;
}
