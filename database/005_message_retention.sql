-- Run manually after 002, 003 and 004, with a backup and the intended database selected.
-- Default is OFF. No existing messages receive an expiry through this migration.
CREATE TABLE IF NOT EXISTS user_message_retention (
    uid INT UNSIGNED NOT NULL PRIMARY KEY,
    seconds INT UNSIGNED NOT NULL DEFAULT 0,
    CONSTRAINT fk_retention_user FOREIGN KEY (uid) REFERENCES user(uid),
    CONSTRAINT chk_retention_seconds CHECK (seconds IN (0,86400,604800,2592000))
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS message_expiry (
    message_id BIGINT UNSIGNED NOT NULL PRIMARY KEY,
    expires_at TIMESTAMP(3) NOT NULL,
    KEY idx_expiry_due (expires_at,message_id),
    CONSTRAINT fk_expiry_message FOREIGN KEY (message_id) REFERENCES chat_message(message_id)
) ENGINE=InnoDB;
