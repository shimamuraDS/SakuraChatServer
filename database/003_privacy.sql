-- Select the intended database and back it up before manual execution.
-- Additive only: never deletes existing users or messages.
CREATE TABLE IF NOT EXISTS user_privacy (
  uid INT UNSIGNED NOT NULL PRIMARY KEY,
  search_policy TINYINT UNSIGNED NOT NULL DEFAULT 0,
  request_policy TINYINT UNSIGNED NOT NULL DEFAULT 0,
  profile_policy TINYINT UNSIGNED NOT NULL DEFAULT 1,
  read_receipts BOOLEAN NOT NULL DEFAULT TRUE,
  CONSTRAINT fk_privacy_user FOREIGN KEY(uid) REFERENCES user(uid) ON DELETE CASCADE,
  CHECK(search_policy IN (0,1,2)), CHECK(request_policy IN (0,1,2)), CHECK(profile_policy IN (0,1,2))
) ENGINE=InnoDB;
CREATE TABLE IF NOT EXISTS user_block (
  owner_uid INT UNSIGNED NOT NULL,
  blocked_uid INT UNSIGNED NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY(owner_uid,blocked_uid),
  CONSTRAINT fk_block_owner FOREIGN KEY(owner_uid) REFERENCES user(uid) ON DELETE CASCADE,
  CONSTRAINT fk_block_target FOREIGN KEY(blocked_uid) REFERENCES user(uid) ON DELETE CASCADE,
  CHECK(owner_uid <> blocked_uid)
) ENGINE=InnoDB;
