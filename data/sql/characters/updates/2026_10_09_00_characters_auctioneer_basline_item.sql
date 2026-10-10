ALTER TABLE `auctioneer_baseline_item`
  ADD COLUMN `trend_bp` INT NOT NULL DEFAULT 0
  COMMENT 'price_median trend over history window, in basis points'
  AFTER `bid_ratio_class_prior_bp`;

CREATE TABLE IF NOT EXISTS `auctioneer_baseline_history` (
  `faction`       TINYINT UNSIGNED NOT NULL,
  `item_id`       INT UNSIGNED     NOT NULL,
  `suffix`        INT              NOT NULL,
  `snapshot_time` TIMESTAMP        NOT NULL,
  `price_median`  INT UNSIGNED     NOT NULL,
  `price_low`     INT UNSIGNED     NOT NULL,
  `price_high`    INT UNSIGNED     NOT NULL,
  `price_samples` INT UNSIGNED     NOT NULL,
  PRIMARY KEY (`faction`, `item_id`, `suffix`, `snapshot_time`),
  KEY `idx_item_time` (`item_id`, `snapshot_time`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
