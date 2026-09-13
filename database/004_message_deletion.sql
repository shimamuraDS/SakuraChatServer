-- MySQL 8; select the intended database and back up before manual execution.
-- Additive migration. This script does not delete messages.
CREATE TABLE IF NOT EXISTS message_delete_head (
    owner_uid INT UNSIGNED NOT NULL PRIMARY KEY,
    last_event BIGINT UNSIGNED NOT NULL DEFAULT 0,
    CONSTRAINT fk_delete_head_user FOREIGN KEY (owner_uid) REFERENCES user(uid)
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS message_delete_event (
    owner_uid INT UNSIGNED NOT NULL,
    event_seq BIGINT UNSIGNED NOT NULL,
    message_id BIGINT UNSIGNED NOT NULL,
    sender_uid INT UNSIGNED NOT NULL,
    client_msg_id VARCHAR(64) NOT NULL,
    created_at TIMESTAMP(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
    PRIMARY KEY (owner_uid,event_seq),
    UNIQUE KEY uk_delete_owner_message (owner_uid,message_id),
    CONSTRAINT fk_delete_event_head FOREIGN KEY (owner_uid) REFERENCES message_delete_head(owner_uid),
    CONSTRAINT fk_delete_event_message FOREIGN KEY (message_id) REFERENCES chat_message(message_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_0900_ai_ci;
