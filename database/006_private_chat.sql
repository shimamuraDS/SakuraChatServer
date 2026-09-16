-- Back up and select the intended database before executing. Additive migration.
CREATE TABLE IF NOT EXISTS private_device (
  uid INT UNSIGNED NOT NULL PRIMARY KEY,
  identity_key VARCHAR(160) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  CONSTRAINT fk_private_device_user FOREIGN KEY(uid) REFERENCES user(uid) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS private_prekey (
  owner_uid INT UNSIGNED NOT NULL,
  key_id BIGINT UNSIGNED NOT NULL,
  bundle MEDIUMTEXT NOT NULL,
  claimant_uid INT UNSIGNED NULL,
  claim_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NULL,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY(owner_uid,key_id),
  UNIQUE KEY uq_private_claim(owner_uid,claimant_uid,claim_id),
  CONSTRAINT fk_private_prekey_device FOREIGN KEY(owner_uid) REFERENCES private_device(uid) ON DELETE CASCADE
) ENGINE=InnoDB;

CREATE TABLE IF NOT EXISTS private_envelope (
  sequence BIGINT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,
  sender_uid INT UNSIGNED NOT NULL,
  recipient_uid INT UNSIGNED NOT NULL,
  message_id CHAR(36) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  sender_identity VARCHAR(160) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  recipient_identity VARCHAR(160) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  kind TINYINT UNSIGNED NOT NULL,
  ciphertext MEDIUMTEXT NULL,
  digest CHAR(64) CHARACTER SET ascii COLLATE ascii_bin NOT NULL,
  acknowledged BOOLEAN NOT NULL DEFAULT FALSE,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  UNIQUE KEY uq_private_message(sender_uid,message_id),
  KEY ix_private_inbox(recipient_uid,acknowledged,sequence),
  CONSTRAINT fk_private_sender FOREIGN KEY(sender_uid) REFERENCES user(uid) ON DELETE CASCADE,
  CONSTRAINT fk_private_recipient FOREIGN KEY(recipient_uid) REFERENCES user(uid) ON DELETE CASCADE,
  CHECK(kind IN (2,3))
) ENGINE=InnoDB;
