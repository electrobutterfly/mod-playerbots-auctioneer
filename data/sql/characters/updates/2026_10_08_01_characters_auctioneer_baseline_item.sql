DROP TABLE IF EXISTS `auctioneer_baseline_item`;
CREATE TABLE `auctioneer_baseline_item` (
  `faction`      TINYINT UNSIGNED NOT NULL COMMENT '2=Alliance, 6=Horde',
  `item_id`      INT UNSIGNED     NOT NULL COMMENT 'item_template.entry',
  `suffix`       INT              NOT NULL COMMENT 'item suffix: <0 enchant, >0 random prop, 0 none',

  -- price block: per-unit buyout, copper
  `price_median`  INT UNSIGNED NOT NULL COMMENT 'adjusted median per-unit buyout',
  `price_low`     INT UNSIGNED NOT NULL COMMENT 'adjusted low (band bottom)',
  `price_high`    INT UNSIGNED NOT NULL COMMENT 'adjusted high (band top)',
  `price_q1`      INT UNSIGNED NOT NULL COMMENT '25th percentile (bid-valuation floor)',
  `price_q3`      INT UNSIGNED NOT NULL COMMENT '75th percentile (buy ceiling)',
  `price_samples` INT UNSIGNED NOT NULL COMMENT 'buyout listings behind the price stats',

  -- stack block
  `stack_typical` INT UNSIGNED NOT NULL COMMENT 'adjusted mode of listing stack size',
  `stack_low`     INT UNSIGNED NOT NULL,
  `stack_high`    INT UNSIGNED NOT NULL,

  -- listing-depth block
  `listing_typical`   INT UNSIGNED NOT NULL COMMENT 'typical concurrent listings per snapshot',
  `listing_snapshots` INT UNSIGNED NOT NULL COMMENT 'how many snapshots the depth came from',

  -- bid-ratio block: MINBID/BUYOUT in basis points (fraction * 10000)
  `bid_ratio_median`  INT UNSIGNED NOT NULL COMMENT 'adjusted median',
  `bid_ratio_low`     INT UNSIGNED NOT NULL,
  `bid_ratio_high`    INT UNSIGNED NOT NULL,
  `bid_ratio_samples` INT UNSIGNED NOT NULL COMMENT 'listings with both buyout and minbid',
  `bid_ratio_reliable` TINYINT(1)  NOT NULL COMMENT '1 if samples >= 10, else 0',
  `bid_ratio_class_prior_bp` INT UNSIGNED NOT NULL COMMENT 'class mean for thin rows, else 0',

  -- provenance
  `snapshot`  VARCHAR(32) NOT NULL COMMENT 'YYYY-MM-DD_HH of source (UTC)',
  `imported_at` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,

  PRIMARY KEY (`faction`, `item_id`, `suffix`),
  KEY `idx_item` (`item_id`)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
