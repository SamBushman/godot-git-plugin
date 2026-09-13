#ifndef GIT_COMMON_H
#define GIT_COMMON_H

#include <cstdio>

#include <Godot.hpp>

#include <git2.h>

struct Credentials {
	godot::String username;
	godot::String password;
	godot::String ssh_public_key_path;
	godot::String ssh_private_key_path;
	godot::String ssh_passphrase;
};

void check_git2_errors(int error, const char *message, const char *extra);

extern "C" int diff_line_callback_function(const git_diff_delta *delta, const git_diff_hunk *hunk, const git_diff_line *line, void *payload);

// Remote operation (fetch/pull/push) callbacks.
extern "C" int progress_cb(const char *str, int len, void *data);
extern "C" int update_cb(const char *refname, const git_oid *a, const git_oid *b, void *data);
extern "C" int transfer_progress_cb(const git_indexer_progress *stats, void *payload);
extern "C" int fetchhead_foreach_cb(const char *ref_name, const char *remote_url, const git_oid *oid, unsigned int is_merge, void *payload);
extern "C" int credentials_cb(git_credential **out, const char *url, const char *username_from_url, unsigned int allowed_types, void *payload);
extern "C" int push_transfer_progress_cb(unsigned int current, unsigned int total, size_t bytes, void *payload);
extern "C" int push_update_reference_cb(const char *refname, const char *status, void *data);

#define GIT2_CALL(function_call, m_error_msg, m_additional_msg) check_git2_errors(function_call, m_error_msg, m_additional_msg);

#endif // !GIT_COMMON_H
