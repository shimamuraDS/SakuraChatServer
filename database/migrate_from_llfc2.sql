-- Non-destructive data migration from the legacy llfc2 schema.
-- Prerequisites:
--   1. Import llfc2.sql into database `llfc`.
--   2. Run database/schema.sql to rebuild database `skrchat`.
--   3. Back up both databases before running this script.
--
-- SECURITY: legacy `user.pwd` values are copied as-is for application
-- compatibility. They must be upgraded to password hashes together with the
-- C++ authentication code; SQL alone cannot safely derive a hash without the
-- user's original password verification flow.

SET NAMES utf8mb4;
SET time_zone = '+00:00';

START TRANSACTION;

INSERT INTO `skrchat`.`user`
  (`uid`, `name`, `email`, `pwd`, `nick`, `desc`, `gender`, `icon`,
   `background`, `status`, `password_version`)
SELECT
  CAST(u.`uid` AS UNSIGNED),
  LEFT(TRIM(u.`name`), 64),
  LEFT(LOWER(TRIM(u.`email`)), 254),
  u.`pwd`,
  LEFT(COALESCE(u.`nick`, ''), 64),
  LEFT(COALESCE(u.`desc`, ''), 500),
  CASE WHEN u.`sex` IN (0, 1, 2, 9) THEN u.`sex` ELSE 0 END,
  LEFT(COALESCE(u.`icon`, ''), 512),
  '',
  0,
  1
FROM `llfc`.`user` u
WHERE u.`uid` > 0
ON DUPLICATE KEY UPDATE
  `name` = VALUES(`name`),
  `email` = VALUES(`email`),
  `pwd` = VALUES(`pwd`),
  `nick` = VALUES(`nick`),
  `desc` = VALUES(`desc`),
  `gender` = VALUES(`gender`),
  `icon` = VALUES(`icon`),
  `updated_at` = CURRENT_TIMESTAMP(3);

UPDATE `skrchat`.`user_id`
   SET `id` = GREATEST(
     `id`,
     COALESCE((SELECT MAX(u.`uid`) FROM `skrchat`.`user` u), `id`),
     COALESCE((SELECT MAX(s.`id`) FROM `llfc`.`user_id` s), `id`)
   )
 WHERE `singleton_id` = 1;

INSERT INTO `skrchat`.`friend`
  (`self_uid`, `friend_uid`, `remark`, `source`)
SELECT
  f.`self_id`,
  f.`friend_id`,
  LEFT(COALESCE(f.`back`, ''), 64),
  0
FROM `llfc`.`friend` f
JOIN `skrchat`.`user` su ON su.`uid` = f.`self_id`
JOIN `skrchat`.`user` fu ON fu.`uid` = f.`friend_id`
WHERE f.`self_id` <> f.`friend_id`
ON DUPLICATE KEY UPDATE
  `remark` = VALUES(`remark`),
  `updated_at` = CURRENT_TIMESTAMP(3);

INSERT INTO `skrchat`.`friend_apply`
  (`from_uid`, `to_uid`, `status`, `descs`, `back_name`, `created_at`, `handled_at`)
SELECT
  a.`from_uid`,
  a.`to_uid`,
  CASE WHEN a.`status` = 1 THEN 1 ELSE 0 END,
  LEFT(COALESCE(a.`descs`, ''), 255),
  LEFT(COALESCE(a.`back_name`, ''), 64),
  CURRENT_TIMESTAMP(3),
  CASE WHEN a.`status` = 1 THEN CURRENT_TIMESTAMP(3) ELSE NULL END
FROM `llfc`.`friend_apply` a
JOIN `skrchat`.`user` fu ON fu.`uid` = a.`from_uid`
JOIN `skrchat`.`user` tu ON tu.`uid` = a.`to_uid`
WHERE a.`from_uid` <> a.`to_uid`
ON DUPLICATE KEY UPDATE
  `status` = VALUES(`status`),
  `descs` = VALUES(`descs`),
  `back_name` = VALUES(`back_name`),
  `handled_at` = VALUES(`handled_at`),
  `updated_at` = CURRENT_TIMESTAMP(3);

INSERT INTO `skrchat`.`chat_thread`
  (`id`, `type`, `status`, `last_seq`, `last_message_id`, `created_at`)
SELECT
  t.`id`,
  t.`type`,
  0,
  0,
  NULL,
  t.`created_at`
FROM `llfc`.`chat_thread` t
ON DUPLICATE KEY UPDATE
  `type` = VALUES(`type`),
  `created_at` = LEAST(`created_at`, VALUES(`created_at`));

INSERT INTO `skrchat`.`private_chat`
  (`thread_id`, `user1_uid`, `user2_uid`, `created_at`)
SELECT
  p.`thread_id`,
  LEAST(p.`user1_id`, p.`user2_id`),
  GREATEST(p.`user1_id`, p.`user2_id`),
  p.`created_at`
FROM `llfc`.`private_chat` p
JOIN `skrchat`.`chat_thread` t
  ON t.`id` = p.`thread_id` AND t.`type` = 'private'
JOIN `skrchat`.`user` u1
  ON u1.`uid` = LEAST(p.`user1_id`, p.`user2_id`)
JOIN `skrchat`.`user` u2
  ON u2.`uid` = GREATEST(p.`user1_id`, p.`user2_id`)
WHERE p.`user1_id` <> p.`user2_id`
ON DUPLICATE KEY UPDATE
  `created_at` = LEAST(`created_at`, VALUES(`created_at`));

INSERT INTO `skrchat`.`group_chat`
  (`thread_id`, `owner_uid`, `name`, `created_at`)
SELECT
  g.`thread_id`,
  owner_user.`uid`,
  LEFT(COALESCE(NULLIF(TRIM(g.`name`), ''), CONCAT('Group ', g.`thread_id`)), 100),
  g.`created_at`
FROM `llfc`.`group_chat` g
JOIN `skrchat`.`chat_thread` t
  ON t.`id` = g.`thread_id` AND t.`type` = 'group'
LEFT JOIN (
  SELECT gm.`thread_id`, MIN(gm.`user_id`) AS `owner_uid`
  FROM `llfc`.`group_chat_member` gm
  WHERE gm.`role` = 2
  GROUP BY gm.`thread_id`
) legacy_owner ON legacy_owner.`thread_id` = g.`thread_id`
LEFT JOIN `skrchat`.`user` owner_user
  ON owner_user.`uid` = legacy_owner.`owner_uid`
ON DUPLICATE KEY UPDATE
  `owner_uid` = COALESCE(VALUES(`owner_uid`), `owner_uid`),
  `name` = VALUES(`name`),
  `updated_at` = CURRENT_TIMESTAMP(3);

INSERT INTO `skrchat`.`group_chat_member`
  (`thread_id`, `user_uid`, `role`, `status`, `joined_at`, `muted_until`)
SELECT
  gm.`thread_id`,
  gm.`user_id`,
  CASE WHEN gm.`role` IN (0, 1, 2) THEN gm.`role` ELSE 0 END,
  0,
  gm.`joined_at`,
  gm.`muted_until`
FROM `llfc`.`group_chat_member` gm
JOIN `skrchat`.`group_chat` g ON g.`thread_id` = gm.`thread_id`
JOIN `skrchat`.`user` u ON u.`uid` = gm.`user_id`
ON DUPLICATE KEY UPDATE
  `role` = VALUES(`role`),
  `status` = 0,
  `muted_until` = VALUES(`muted_until`),
  `updated_at` = CURRENT_TIMESTAMP(3);

INSERT INTO `skrchat`.`chat_message`
  (`message_id`, `thread_id`, `seq`, `client_msg_id`, `sender_uid`,
   `receiver_uid`, `message_type`, `content`, `extra`, `status`,
   `created_at`, `updated_at`)
SELECT
  ranked.`message_id`,
  ranked.`thread_id`,
  ranked.`seq`,
  CONCAT('legacy-', ranked.`message_id`),
  ranked.`sender_id`,
  receiver_user.`uid`,
  1,
  ranked.`content`,
  JSON_OBJECT('legacy_status', ranked.`legacy_status`),
  CASE WHEN ranked.`legacy_status` = 2 THEN 1 ELSE 0 END,
  ranked.`created_at`,
  ranked.`updated_at`
FROM (
  SELECT
    m.`message_id`,
    m.`thread_id`,
    m.`sender_id`,
    m.`recv_id`,
    m.`content`,
    m.`created_at`,
    m.`updated_at`,
    m.`status` AS `legacy_status`,
    ROW_NUMBER() OVER (
      PARTITION BY m.`thread_id`
      ORDER BY m.`created_at`, m.`message_id`
    ) AS `seq`
  FROM `llfc`.`chat_message` m
) ranked
JOIN `skrchat`.`chat_thread` t ON t.`id` = ranked.`thread_id`
JOIN `skrchat`.`user` sender_user ON sender_user.`uid` = ranked.`sender_id`
LEFT JOIN `skrchat`.`user` receiver_user ON receiver_user.`uid` = ranked.`recv_id`
ON DUPLICATE KEY UPDATE
  `thread_id` = VALUES(`thread_id`),
  `seq` = VALUES(`seq`),
  `receiver_uid` = VALUES(`receiver_uid`),
  `content` = VALUES(`content`),
  `extra` = VALUES(`extra`),
  `status` = VALUES(`status`),
  `updated_at` = VALUES(`updated_at`);

UPDATE `skrchat`.`chat_thread` t
JOIN (
  SELECT m.`thread_id`, MAX(m.`seq`) AS `last_seq`
  FROM `skrchat`.`chat_message` m
  GROUP BY m.`thread_id`
) summary ON summary.`thread_id` = t.`id`
JOIN `skrchat`.`chat_message` last_message
  ON last_message.`thread_id` = summary.`thread_id`
 AND last_message.`seq` = summary.`last_seq`
SET
  t.`last_seq` = summary.`last_seq`,
  t.`last_message_id` = last_message.`message_id`,
  t.`updated_at` = GREATEST(t.`updated_at`, last_message.`created_at`);

INSERT INTO `skrchat`.`message_receipt`
  (`message_id`, `user_uid`, `status`, `delivered_at`, `read_at`)
SELECT
  target_message.`message_id`,
  target_message.`receiver_uid`,
  1,
  target_message.`created_at`,
  target_message.`updated_at`
FROM `skrchat`.`chat_message` target_message
JOIN `llfc`.`chat_message` legacy_message
  ON legacy_message.`message_id` = target_message.`message_id`
WHERE legacy_message.`status` = 1
  AND target_message.`receiver_uid` IS NOT NULL
ON DUPLICATE KEY UPDATE
  `status` = 1,
  `read_at` = VALUES(`read_at`);

INSERT INTO `skrchat`.`schema_migrations` (`version`, `description`)
VALUES ('legacy-llfc2', 'Imported data from legacy llfc2 schema')
ON DUPLICATE KEY UPDATE
  `description` = VALUES(`description`);

COMMIT;

-- Post-migration summary. Review all counts before switching application config.
SELECT 'user' AS `table_name`, COUNT(*) AS `row_count` FROM `skrchat`.`user`
UNION ALL
SELECT 'friend', COUNT(*) FROM `skrchat`.`friend`
UNION ALL
SELECT 'friend_apply', COUNT(*) FROM `skrchat`.`friend_apply`
UNION ALL
SELECT 'chat_thread', COUNT(*) FROM `skrchat`.`chat_thread`
UNION ALL
SELECT 'private_chat', COUNT(*) FROM `skrchat`.`private_chat`
UNION ALL
SELECT 'group_chat', COUNT(*) FROM `skrchat`.`group_chat`
UNION ALL
SELECT 'group_chat_member', COUNT(*) FROM `skrchat`.`group_chat_member`
UNION ALL
SELECT 'chat_message', COUNT(*) FROM `skrchat`.`chat_message`;

SELECT
  (SELECT COUNT(*) FROM `llfc`.`user`) AS `legacy_users`,
  (SELECT COUNT(*) FROM `skrchat`.`user`) AS `target_users`,
  (SELECT COUNT(*) FROM `llfc`.`chat_message`) AS `legacy_messages`,
  (SELECT COUNT(*) FROM `skrchat`.`chat_message`) AS `target_messages`;
