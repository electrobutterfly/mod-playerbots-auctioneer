DROP TABLE IF EXISTS `auctioneer_baseline_category`;
CREATE TABLE `auctioneer_baseline_category` (
  `faction`     TINYINT UNSIGNED NOT NULL,
  `item_class`  TINYINT UNSIGNED NOT NULL COMMENT '0..15 (AzerothCore ITEM_CLASS_*)',
  `quality`     TINYINT UNSIGNED NOT NULL COMMENT '0..7 (AzerothCore ITEM_QUALITY_*)',
  `sample_count` INT UNSIGNED NOT NULL COMMENT 'snapshots the category appeared in',
  `q1`          INT UNSIGNED NOT NULL,
  `median`      INT UNSIGNED NOT NULL,
  `adj_low`     INT UNSIGNED NOT NULL,
  `adj_high`    INT UNSIGNED NOT NULL,
  `snapshot`    VARCHAR(32) NOT NULL,
  `imported_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  PRIMARY KEY (`faction`, `item_class`, `quality`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
