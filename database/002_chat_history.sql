-- Additive setup for the selected SakuraChat database (MySQL 8.0+).
-- Select the intended database before running. Existing tables/data are NOT replaced.
-- Requires the existing user(uid) table. No DROP, TRUNCATE, or data migration.

CREATE TABLE IF NOT EXISTS `chat_thread` (
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

CREATE TABLE IF NOT EXISTS `private_chat` (
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

CREATE TABLE IF NOT EXISTS `chat_message` (
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

CREATE TABLE IF NOT EXISTS `message_receipt` (
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
