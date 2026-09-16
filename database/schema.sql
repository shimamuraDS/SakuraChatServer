-- SakuraChat database schema
-- Target: MySQL 8.0.27+
-- DESTRUCTIVE REPLACEMENT: drops and recreates the existing `skrchat` database.
-- Contains no legacy users or plaintext sample passwords.

SET NAMES utf8mb4;
SET time_zone = '+00:00';

DROP DATABASE IF EXISTS `skrchat`;

CREATE DATABASE `skrchat`
  CHARACTER SET utf8mb4
  COLLATE utf8mb4_0900_ai_ci;

USE `skrchat`;

SET FOREIGN_KEY_CHECKS = 0;

DROP PROCEDURE IF EXISTS `create_chat_message`;
DROP PROCEDURE IF EXISTS `create_private_chat`;
DROP PROCEDURE IF EXISTS `resolve_friend_apply`;
DROP PROCEDURE IF EXISTS `apply_friend`;
DROP PROCEDURE IF EXISTS `reg_user`;

DROP TABLE IF EXISTS `chat_outbox`;
DROP TABLE IF EXISTS `message_attachment`;
DROP TABLE IF EXISTS `message_receipt`;
DROP TABLE IF EXISTS `chat_message`;
DROP TABLE IF EXISTS `group_chat_member`;
DROP TABLE IF EXISTS `group_chat`;
DROP TABLE IF EXISTS `private_chat`;
DROP TABLE IF EXISTS `chat_thread`;
DROP TABLE IF EXISTS `friend_apply`;
DROP TABLE IF EXISTS `friend`;
DROP TABLE IF EXISTS `user_id`;
DROP TABLE IF EXISTS `user`;
DROP TABLE IF EXISTS `schema_migrations`;

SET FOREIGN_KEY_CHECKS = 1;

CREATE TABLE `schema_migrations` (
  `version` varchar(64) NOT NULL,
  `description` varchar(255) NOT NULL,
  `installed_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`version`)
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;

CREATE TABLE `user` (
  `id` bigint UNSIGNED NOT NULL AUTO_INCREMENT COMMENT '数据库内部主键',
  `uid` int UNSIGNED NOT NULL COMMENT '面向业务和协议的稳定用户ID',
  `name` varchar(64) NOT NULL COMMENT '登录名',
  `email` varchar(254) NOT NULL COMMENT '登录邮箱，按不区分大小写排序规则唯一',
  `pwd` varchar(255) NOT NULL COMMENT '兼容现有DAO；应用升级后仅允许存密码哈希',
  `nick` varchar(64) NOT NULL DEFAULT '' COMMENT '显示昵称',
  `desc` varchar(500) NOT NULL DEFAULT '' COMMENT '个人简介',
  `gender` tinyint UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=未设置,1=男,2=女,9=其他',
  `icon` varchar(512) NOT NULL DEFAULT '' COMMENT '头像资源地址',
  `background` varchar(512) NOT NULL DEFAULT '' COMMENT '个人背景资源地址',
  `status` tinyint UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=正常,1=禁用,2=注销',
  `password_version` int UNSIGNED NOT NULL DEFAULT 1 COMMENT '密码或会话全量失效时递增',
  `created_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `updated_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  `deleted_at` timestamp(3) NULL DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_user_uid` (`uid`),
  UNIQUE KEY `uk_user_name` (`name`),
  UNIQUE KEY `uk_user_email` (`email`),
  KEY `idx_user_status_updated` (`status`, `updated_at`),
  CONSTRAINT `chk_user_uid` CHECK (`uid` > 0),
  CONSTRAINT `chk_user_gender` CHECK (`gender` IN (0, 1, 2, 9)),
  CONSTRAINT `chk_user_status` CHECK (`status` IN (0, 1, 2))
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci
  ROW_FORMAT = DYNAMIC;

CREATE TABLE `user_id` (
  `singleton_id` tinyint UNSIGNED NOT NULL DEFAULT 1,
  `id` int UNSIGNED NOT NULL COMMENT '最后一个已分配业务UID',
  `updated_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`singleton_id`),
  UNIQUE KEY `uk_user_id_value` (`id`),
  CONSTRAINT `chk_user_id_singleton` CHECK (`singleton_id` = 1),
  CONSTRAINT `chk_user_id_range` CHECK (`id` < 2147483647)
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;

INSERT INTO `user_id` (`singleton_id`, `id`)
VALUES (1, 1000);

CREATE TABLE `friend` (
  `id` bigint UNSIGNED NOT NULL AUTO_INCREMENT,
  `self_uid` int UNSIGNED NOT NULL,
  `friend_uid` int UNSIGNED NOT NULL,
  `remark` varchar(64) NOT NULL DEFAULT '' COMMENT '当前用户给好友设置的备注',
  `source` tinyint UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=未知,1=搜索,2=群聊,3=名片',
  `created_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `updated_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_friend_pair` (`self_uid`, `friend_uid`),
  KEY `idx_friend_reverse` (`friend_uid`, `self_uid`),
  CONSTRAINT `fk_friend_self` FOREIGN KEY (`self_uid`) REFERENCES `user` (`uid`) ON DELETE CASCADE,
  CONSTRAINT `fk_friend_target` FOREIGN KEY (`friend_uid`) REFERENCES `user` (`uid`) ON DELETE CASCADE,
  CONSTRAINT `chk_friend_not_self` CHECK (`self_uid` <> `friend_uid`),
  CONSTRAINT `chk_friend_source` CHECK (`source` BETWEEN 0 AND 3)
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;

CREATE TABLE `friend_apply` (
  `id` bigint UNSIGNED NOT NULL AUTO_INCREMENT,
  `from_uid` int UNSIGNED NOT NULL,
  `to_uid` int UNSIGNED NOT NULL,
  `status` tinyint UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=待处理,1=同意,2=拒绝,3=撤销',
  `descs` varchar(255) NOT NULL DEFAULT '' COMMENT '申请附言，兼容原字段名',
  `back_name` varchar(64) NOT NULL DEFAULT '' COMMENT '申请方预设好友备注',
  `created_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `updated_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  `handled_at` timestamp(3) NULL DEFAULT NULL,
  PRIMARY KEY (`id`),
  UNIQUE KEY `uk_friend_apply_pair` (`from_uid`, `to_uid`),
  KEY `idx_friend_apply_inbox` (`to_uid`, `status`, `created_at`),
  KEY `idx_friend_apply_outbox` (`from_uid`, `status`, `created_at`),
  CONSTRAINT `fk_friend_apply_from` FOREIGN KEY (`from_uid`) REFERENCES `user` (`uid`) ON DELETE CASCADE,
  CONSTRAINT `fk_friend_apply_to` FOREIGN KEY (`to_uid`) REFERENCES `user` (`uid`) ON DELETE CASCADE,
  CONSTRAINT `chk_friend_apply_not_self` CHECK (`from_uid` <> `to_uid`),
  CONSTRAINT `chk_friend_apply_status` CHECK (`status` IN (0, 1, 2, 3))
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;

CREATE TABLE `chat_thread` (
  `id` bigint UNSIGNED NOT NULL AUTO_INCREMENT,
  `type` enum('private', 'group') NOT NULL,
  `status` tinyint UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=正常,1=只读,2=关闭',
  `last_seq` bigint UNSIGNED NOT NULL DEFAULT 0 COMMENT '会话内最后分配的消息序号',
  `last_message_id` bigint UNSIGNED NULL DEFAULT NULL,
  `created_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `updated_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  KEY `idx_chat_thread_updated` (`updated_at`),
  CONSTRAINT `chk_chat_thread_status` CHECK (`status` IN (0, 1, 2))
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;

CREATE TABLE `private_chat` (
  `thread_id` bigint UNSIGNED NOT NULL,
  `user1_uid` int UNSIGNED NOT NULL COMMENT '两位用户中较小的UID',
  `user2_uid` int UNSIGNED NOT NULL COMMENT '两位用户中较大的UID',
  `created_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`thread_id`),
  UNIQUE KEY `uk_private_chat_pair` (`user1_uid`, `user2_uid`),
  KEY `idx_private_chat_user2` (`user2_uid`, `thread_id`),
  CONSTRAINT `fk_private_chat_thread` FOREIGN KEY (`thread_id`) REFERENCES `chat_thread` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_private_chat_user1` FOREIGN KEY (`user1_uid`) REFERENCES `user` (`uid`) ON DELETE CASCADE,
  CONSTRAINT `fk_private_chat_user2` FOREIGN KEY (`user2_uid`) REFERENCES `user` (`uid`) ON DELETE CASCADE,
  CONSTRAINT `chk_private_chat_order` CHECK (`user1_uid` < `user2_uid`)
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;

CREATE TABLE `group_chat` (
  `thread_id` bigint UNSIGNED NOT NULL,
  `owner_uid` int UNSIGNED NULL DEFAULT NULL COMMENT '迁移旧数据时允许为空',
  `name` varchar(100) NOT NULL,
  `icon` varchar(512) NOT NULL DEFAULT '',
  `announcement` varchar(1000) NOT NULL DEFAULT '',
  `max_members` int UNSIGNED NOT NULL DEFAULT 500,
  `join_policy` tinyint UNSIGNED NOT NULL DEFAULT 1 COMMENT '0=禁止加入,1=需审核,2=允许加入',
  `created_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `updated_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`thread_id`),
  KEY `idx_group_chat_owner` (`owner_uid`),
  CONSTRAINT `fk_group_chat_thread` FOREIGN KEY (`thread_id`) REFERENCES `chat_thread` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_group_chat_owner` FOREIGN KEY (`owner_uid`) REFERENCES `user` (`uid`) ON DELETE SET NULL,
  CONSTRAINT `chk_group_chat_max_members` CHECK (`max_members` BETWEEN 2 AND 10000),
  CONSTRAINT `chk_group_chat_join_policy` CHECK (`join_policy` IN (0, 1, 2))
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;

CREATE TABLE `group_chat_member` (
  `thread_id` bigint UNSIGNED NOT NULL,
  `user_uid` int UNSIGNED NOT NULL,
  `role` tinyint UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=成员,1=管理员,2=群主',
  `status` tinyint UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=正常,1=退出,2=移除',
  `last_read_seq` bigint UNSIGNED NOT NULL DEFAULT 0,
  `joined_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `updated_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  `muted_until` timestamp(3) NULL DEFAULT NULL,
  PRIMARY KEY (`thread_id`, `user_uid`),
  KEY `idx_group_member_user` (`user_uid`, `status`, `updated_at`),
  CONSTRAINT `fk_group_member_group` FOREIGN KEY (`thread_id`) REFERENCES `group_chat` (`thread_id`) ON DELETE CASCADE,
  CONSTRAINT `fk_group_member_user` FOREIGN KEY (`user_uid`) REFERENCES `user` (`uid`) ON DELETE CASCADE,
  CONSTRAINT `chk_group_member_role` CHECK (`role` IN (0, 1, 2)),
  CONSTRAINT `chk_group_member_status` CHECK (`status` IN (0, 1, 2))
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;

CREATE TABLE `chat_message` (
  `message_id` bigint UNSIGNED NOT NULL AUTO_INCREMENT,
  `thread_id` bigint UNSIGNED NOT NULL,
  `seq` bigint UNSIGNED NOT NULL COMMENT '会话内严格递增序号',
  `client_msg_id` varchar(64) NOT NULL COMMENT '客户端幂等键',
  `sender_uid` int UNSIGNED NOT NULL,
  `receiver_uid` int UNSIGNED NULL DEFAULT NULL COMMENT '私聊接收者；群聊为空',
  `message_type` tinyint UNSIGNED NOT NULL DEFAULT 1 COMMENT '1=文本,2=图片,3=文件,4=系统',
  `content` mediumtext NOT NULL,
  `extra` json NULL DEFAULT NULL,
  `status` tinyint UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=正常,1=撤回,2=删除',
  `created_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `updated_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3) ON UPDATE CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`message_id`),
  UNIQUE KEY `uk_chat_message_seq` (`thread_id`, `seq`),
  UNIQUE KEY `uk_chat_message_idempotency` (`sender_uid`, `client_msg_id`),
  KEY `idx_chat_message_thread_id` (`thread_id`, `message_id`),
  KEY `idx_chat_message_receiver` (`receiver_uid`, `created_at`),
  CONSTRAINT `fk_chat_message_thread` FOREIGN KEY (`thread_id`) REFERENCES `chat_thread` (`id`) ON DELETE CASCADE,
  CONSTRAINT `fk_chat_message_sender` FOREIGN KEY (`sender_uid`) REFERENCES `user` (`uid`) ON DELETE RESTRICT,
  CONSTRAINT `fk_chat_message_receiver` FOREIGN KEY (`receiver_uid`) REFERENCES `user` (`uid`) ON DELETE SET NULL,
  CONSTRAINT `chk_chat_message_type` CHECK (`message_type` IN (1, 2, 3, 4)),
  CONSTRAINT `chk_chat_message_status` CHECK (`status` IN (0, 1, 2))
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci
  ROW_FORMAT = DYNAMIC;

CREATE TABLE `message_receipt` (
  `message_id` bigint UNSIGNED NOT NULL,
  `user_uid` int UNSIGNED NOT NULL,
  `status` tinyint UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=已投递,1=已读',
  `delivered_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `read_at` timestamp(3) NULL DEFAULT NULL,
  PRIMARY KEY (`message_id`, `user_uid`),
  KEY `idx_receipt_user_status` (`user_uid`, `status`, `message_id`),
  CONSTRAINT `fk_receipt_message` FOREIGN KEY (`message_id`) REFERENCES `chat_message` (`message_id`) ON DELETE CASCADE,
  CONSTRAINT `fk_receipt_user` FOREIGN KEY (`user_uid`) REFERENCES `user` (`uid`) ON DELETE CASCADE,
  CONSTRAINT `chk_receipt_status` CHECK (`status` IN (0, 1))
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;

CREATE TABLE `message_attachment` (
  `id` bigint UNSIGNED NOT NULL AUTO_INCREMENT,
  `message_id` bigint UNSIGNED NOT NULL,
  `storage_key` varchar(512) NOT NULL COMMENT '对象存储key，不保存本地绝对路径',
  `file_name` varchar(255) NOT NULL DEFAULT '',
  `mime_type` varchar(127) NOT NULL DEFAULT 'application/octet-stream',
  `size_bytes` bigint UNSIGNED NOT NULL DEFAULT 0,
  `sha256` char(64) NULL DEFAULT NULL,
  `created_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  KEY `idx_attachment_message` (`message_id`),
  CONSTRAINT `fk_attachment_message` FOREIGN KEY (`message_id`) REFERENCES `chat_message` (`message_id`) ON DELETE CASCADE
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;

CREATE TABLE `chat_outbox` (
  `id` bigint UNSIGNED NOT NULL AUTO_INCREMENT,
  `aggregate_type` varchar(32) NOT NULL,
  `aggregate_id` varchar(64) NOT NULL,
  `event_type` varchar(64) NOT NULL,
  `payload` json NOT NULL,
  `status` tinyint UNSIGNED NOT NULL DEFAULT 0 COMMENT '0=待处理,1=处理中,2=完成,3=失败',
  `attempts` int UNSIGNED NOT NULL DEFAULT 0,
  `available_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  `processed_at` timestamp(3) NULL DEFAULT NULL,
  `created_at` timestamp(3) NOT NULL DEFAULT CURRENT_TIMESTAMP(3),
  PRIMARY KEY (`id`),
  KEY `idx_outbox_dispatch` (`status`, `available_at`, `id`),
  CONSTRAINT `chk_outbox_status` CHECK (`status` IN (0, 1, 2, 3))
) ENGINE = InnoDB
  DEFAULT CHARACTER SET = utf8mb4
  COLLATE = utf8mb4_0900_ai_ci;

DELIMITER $$

-- Compatibility contract used by MysqlDao::RegUser:
-- positive UID = success, 0 = duplicate name/email, -1 = internal failure.
CREATE PROCEDURE `reg_user`(
  IN `new_name` varchar(64),
  IN `new_email` varchar(254),
  IN `new_pwd` varchar(255),
  OUT `result` int
)
proc: BEGIN
  DECLARE v_uid int UNSIGNED DEFAULT 0;

  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_uid = 0;

  DECLARE EXIT HANDLER FOR 1062
  BEGIN
    ROLLBACK;
    SET result = 0;
  END;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    SET result = -1;
  END;

  SET result = -1;

  IF new_name IS NULL OR CHAR_LENGTH(TRIM(new_name)) = 0
     OR new_email IS NULL OR CHAR_LENGTH(TRIM(new_email)) = 0
     OR new_pwd IS NULL OR CHAR_LENGTH(new_pwd) = 0 THEN
    SET result = -1;
    LEAVE proc;
  END IF;

  START TRANSACTION;

  IF EXISTS (
    SELECT 1
    FROM `user`
    WHERE `name` = TRIM(new_name)
       OR `email` = LOWER(TRIM(new_email))
    LIMIT 1
  ) THEN
    ROLLBACK;
    SET result = 0;
    LEAVE proc;
  END IF;

  SELECT `id` + 1
    INTO v_uid
    FROM `user_id`
   WHERE `singleton_id` = 1
   FOR UPDATE;

  IF v_uid = 0 OR v_uid > 2147483647 THEN
    SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = 'UID sequence exhausted';
  END IF;

  UPDATE `user_id`
     SET `id` = v_uid
   WHERE `singleton_id` = 1;

  INSERT INTO `user` (`uid`, `name`, `email`, `pwd`, `nick`)
  VALUES (v_uid, TRIM(new_name), LOWER(TRIM(new_email)), new_pwd, TRIM(new_name));

  COMMIT;
  SET result = v_uid;
END$$

CREATE PROCEDURE `apply_friend`(
  IN `p_from_uid` int UNSIGNED,
  IN `p_to_uid` int UNSIGNED,
  IN `p_descs` varchar(255),
  IN `p_back_name` varchar(64),
  OUT `result` int
)
proc: BEGIN
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    SET result = -1;
  END;

  SET result = -1;

  IF p_from_uid IS NULL OR p_to_uid IS NULL OR p_from_uid = p_to_uid THEN
    LEAVE proc;
  END IF;

  IF NOT EXISTS (SELECT 1 FROM `user` WHERE `uid` = p_from_uid AND `status` = 0)
     OR NOT EXISTS (SELECT 1 FROM `user` WHERE `uid` = p_to_uid AND `status` = 0) THEN
    LEAVE proc;
  END IF;

  IF EXISTS (
    SELECT 1 FROM `friend`
    WHERE `self_uid` = p_from_uid AND `friend_uid` = p_to_uid
  ) THEN
    SET result = 1;
    LEAVE proc;
  END IF;

  START TRANSACTION;

  INSERT INTO `friend_apply`
    (`from_uid`, `to_uid`, `status`, `descs`, `back_name`, `handled_at`)
  VALUES
    (p_from_uid, p_to_uid, 0, COALESCE(p_descs, ''), COALESCE(p_back_name, ''), NULL)
  ON DUPLICATE KEY UPDATE
    `status` = 0,
    `descs` = VALUES(`descs`),
    `back_name` = VALUES(`back_name`),
    `handled_at` = NULL,
    `updated_at` = CURRENT_TIMESTAMP(3);

  COMMIT;
  SET result = 0;
END$$

CREATE PROCEDURE `resolve_friend_apply`(
  IN `p_apply_id` bigint UNSIGNED,
  IN `p_actor_uid` int UNSIGNED,
  IN `p_agree` boolean,
  OUT `result` int
)
proc: BEGIN
  DECLARE v_from_uid int UNSIGNED DEFAULT NULL;
  DECLARE v_to_uid int UNSIGNED DEFAULT NULL;
  DECLARE v_status tinyint UNSIGNED DEFAULT NULL;
  DECLARE v_back_name varchar(64) DEFAULT '';
  DECLARE v_not_found boolean DEFAULT FALSE;

  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_not_found = TRUE;
  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    SET result = -1;
  END;

  SET result = -1;
  START TRANSACTION;

  SELECT `from_uid`, `to_uid`, `status`, `back_name`
    INTO v_from_uid, v_to_uid, v_status, v_back_name
    FROM `friend_apply`
   WHERE `id` = p_apply_id
   FOR UPDATE;

  IF v_not_found OR v_to_uid <> p_actor_uid OR v_status <> 0 THEN
    ROLLBACK;
    LEAVE proc;
  END IF;

  IF p_agree THEN
    INSERT INTO `friend` (`self_uid`, `friend_uid`, `remark`)
    VALUES (v_from_uid, v_to_uid, v_back_name)
    ON DUPLICATE KEY UPDATE `remark` = VALUES(`remark`), `updated_at` = CURRENT_TIMESTAMP(3);

    INSERT INTO `friend` (`self_uid`, `friend_uid`, `remark`)
    VALUES (v_to_uid, v_from_uid, '')
    ON DUPLICATE KEY UPDATE `updated_at` = CURRENT_TIMESTAMP(3);

    UPDATE `friend_apply`
       SET `status` = 1, `handled_at` = CURRENT_TIMESTAMP(3)
     WHERE `id` = p_apply_id;
  ELSE
    UPDATE `friend_apply`
       SET `status` = 2, `handled_at` = CURRENT_TIMESTAMP(3)
     WHERE `id` = p_apply_id;
  END IF;

  COMMIT;
  SET result = 0;
END$$

CREATE PROCEDURE `create_private_chat`(
  IN `p_user_a` int UNSIGNED,
  IN `p_user_b` int UNSIGNED,
  OUT `p_thread_id` bigint UNSIGNED,
  OUT `result` int
)
proc: BEGIN
  DECLARE v_user1 int UNSIGNED;
  DECLARE v_user2 int UNSIGNED;
  DECLARE v_existing_thread bigint UNSIGNED DEFAULT NULL;

  DECLARE EXIT HANDLER FOR 1062
  BEGIN
    ROLLBACK;
    SELECT MAX(`thread_id`)
      INTO p_thread_id
      FROM `private_chat`
     WHERE `user1_uid` = v_user1 AND `user2_uid` = v_user2;
    SET result = IF(p_thread_id IS NULL, -1, 0);
  END;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    SET p_thread_id = NULL;
    SET result = -1;
  END;

  SET p_thread_id = NULL;
  SET result = -1;

  IF p_user_a IS NULL OR p_user_b IS NULL OR p_user_a = p_user_b THEN
    LEAVE proc;
  END IF;

  SET v_user1 = LEAST(p_user_a, p_user_b);
  SET v_user2 = GREATEST(p_user_a, p_user_b);

  IF NOT EXISTS (SELECT 1 FROM `user` WHERE `uid` = v_user1 AND `status` = 0)
     OR NOT EXISTS (SELECT 1 FROM `user` WHERE `uid` = v_user2 AND `status` = 0) THEN
    LEAVE proc;
  END IF;

  START TRANSACTION;

  SELECT MAX(`thread_id`)
    INTO v_existing_thread
    FROM `private_chat`
   WHERE `user1_uid` = v_user1 AND `user2_uid` = v_user2;

  IF v_existing_thread IS NOT NULL THEN
    COMMIT;
    SET p_thread_id = v_existing_thread;
    SET result = 0;
    LEAVE proc;
  END IF;

  INSERT INTO `chat_thread` (`type`) VALUES ('private');
  SET p_thread_id = LAST_INSERT_ID();

  INSERT INTO `private_chat` (`thread_id`, `user1_uid`, `user2_uid`)
  VALUES (p_thread_id, v_user1, v_user2);

  COMMIT;
  SET result = 0;
END$$

CREATE PROCEDURE `create_chat_message`(
  IN `p_thread_id` bigint UNSIGNED,
  IN `p_client_msg_id` varchar(64),
  IN `p_sender_uid` int UNSIGNED,
  IN `p_receiver_uid` int UNSIGNED,
  IN `p_message_type` tinyint UNSIGNED,
  IN `p_content` mediumtext,
  IN `p_extra` json,
  OUT `p_message_id` bigint UNSIGNED,
  OUT `p_seq` bigint UNSIGNED,
  OUT `result` int
)
proc: BEGIN
  DECLARE v_thread_type varchar(16) DEFAULT NULL;
  DECLARE v_thread_status tinyint UNSIGNED DEFAULT NULL;
  DECLARE v_last_seq bigint UNSIGNED DEFAULT NULL;
  DECLARE v_existing_message bigint UNSIGNED DEFAULT NULL;
  DECLARE v_existing_seq bigint UNSIGNED DEFAULT NULL;
  DECLARE v_thread_not_found boolean DEFAULT FALSE;

  DECLARE CONTINUE HANDLER FOR NOT FOUND SET v_thread_not_found = TRUE;

  DECLARE EXIT HANDLER FOR SQLEXCEPTION
  BEGIN
    ROLLBACK;
    SET p_message_id = NULL;
    SET p_seq = NULL;
    SET result = -1;
  END;

  SET p_message_id = NULL;
  SET p_seq = NULL;
  SET result = -1;

  IF p_thread_id IS NULL OR p_sender_uid IS NULL
     OR p_client_msg_id IS NULL OR CHAR_LENGTH(p_client_msg_id) = 0
     OR p_content IS NULL OR p_message_type IS NULL
     OR p_message_type NOT IN (1, 2, 3, 4) THEN
    LEAVE proc;
  END IF;

  SELECT MAX(`message_id`), MAX(`seq`)
    INTO v_existing_message, v_existing_seq
    FROM `chat_message`
   WHERE `sender_uid` = p_sender_uid
     AND `client_msg_id` = p_client_msg_id;

  IF v_existing_message IS NOT NULL THEN
    SET p_message_id = v_existing_message;
    SET p_seq = v_existing_seq;
    SET result = 0;
    LEAVE proc;
  END IF;

  START TRANSACTION;

  SELECT `type`, `status`, `last_seq`
    INTO v_thread_type, v_thread_status, v_last_seq
    FROM `chat_thread`
   WHERE `id` = p_thread_id
   FOR UPDATE;

  IF v_thread_not_found THEN
    ROLLBACK;
    LEAVE proc;
  END IF;

  IF v_thread_status <> 0 THEN
    ROLLBACK;
    LEAVE proc;
  END IF;

  IF v_thread_type = 'private' AND NOT EXISTS (
    SELECT 1 FROM `private_chat`
     WHERE `thread_id` = p_thread_id
       AND p_sender_uid IN (`user1_uid`, `user2_uid`)
  ) THEN
    ROLLBACK;
    LEAVE proc;
  END IF;

  IF v_thread_type = 'group' AND NOT EXISTS (
    SELECT 1 FROM `group_chat_member`
     WHERE `thread_id` = p_thread_id
       AND `user_uid` = p_sender_uid
       AND `status` = 0
  ) THEN
    ROLLBACK;
    LEAVE proc;
  END IF;

  SET p_seq = v_last_seq + 1;

  UPDATE `chat_thread`
     SET `last_seq` = p_seq
   WHERE `id` = p_thread_id;

  INSERT INTO `chat_message`
    (`thread_id`, `seq`, `client_msg_id`, `sender_uid`, `receiver_uid`,
     `message_type`, `content`, `extra`)
  VALUES
    (p_thread_id, p_seq, p_client_msg_id, p_sender_uid, p_receiver_uid,
     p_message_type, p_content, p_extra);

  SET p_message_id = LAST_INSERT_ID();

  UPDATE `chat_thread`
     SET `last_message_id` = p_message_id
   WHERE `id` = p_thread_id;

  INSERT INTO `chat_outbox`
    (`aggregate_type`, `aggregate_id`, `event_type`, `payload`)
  VALUES
    ('message', CAST(p_message_id AS CHAR), 'chat.message.created',
     JSON_OBJECT(
       'message_id', CAST(p_message_id AS CHAR),
       'thread_id', CAST(p_thread_id AS CHAR),
       'seq', CAST(p_seq AS CHAR),
       'sender_uid', p_sender_uid,
       'receiver_uid', p_receiver_uid
     ));

  COMMIT;
  SET result = 0;
END$$

DELIMITER ;

INSERT INTO `schema_migrations` (`version`, `description`)
VALUES ('001', 'Initial production-oriented SakuraChat schema');
