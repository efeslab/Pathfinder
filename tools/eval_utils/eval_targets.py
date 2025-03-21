'''
Contains the configuration for the evaluation targets.
'''

from bug_locations import get_bugs

# Maps bug locations to targets

SQUINT_FULL_TARGETS = {
    # PMDK Data Structures
    # -- Array
    'pmdk_array': [
        'pmdk_array_v1.4/squint-config.ini',
        'pmdk_array_v1.4/squint-config-free.ini',
        'pmdk_array_v1.4/squint-config-realloc.ini',
        'pmdk_array_v1.4/squint-config-toid.ini',

        'pmdk_array_v1.8/squint-config.ini',
        'pmdk_array_v1.8/squint-config-free.ini',
        'pmdk_array_v1.8/squint-config-realloc.ini',
        'pmdk_array_v1.8/squint-config-toid.ini',
    ],
    # -- Btree
    'pmdk_btree': [
        'pmdk_btree_jaaru/squint-config.ini',
        'pmdk_btree_map_v1.4/squint-config.ini',
        'pmdk_btree_map_v1.8/squint-config.ini',
    ],
    # -- Ctree
    'pmdk_ctree': [
        'pmdk_ctree_jaaru/squint-config.ini',
        'pmdk_ctree_map_v1.4/squint-config.ini',
        'pmdk_ctree_map_v1.8/squint-config.ini',
    ],
    # -- Hashmap Atomic
    'pmdk_hashmap_atomic': [
        'pmdk_hashmap_atomic_jaaru/squint-config.ini',
        'pmdk_hashmap_atomic_v1.4/squint-config.ini',
        'pmdk_hashmap_atomic_v1.8/squint-config.ini',
    ],
    # -- Hashmap TX
    'pmdk_hashmap_tx': [
        'pmdk_hashmap_tx_jaaru/squint-config.ini',
        'pmdk_hashmap_tx_v1.4/squint-config.ini',
        'pmdk_hashmap_tx_v1.8/squint-config.ini',
    ],
    # -- Rbtree
    'pmdk_rbtree': [
        'pmdk_rbtree_jaaru/squint-config.ini',
        'pmdk_rbtree_map_v1.4/squint-config.ini',
        'pmdk_rbtree_map_v1.4/squint-config-freebug.ini',
        'pmdk_rbtree_map_v1.8/squint-config.ini',
    ],

    # Key-Value Indices
    # -- CCEH
    'cceh': [
        'CCEH/squint-config.ini',
    ],
    # -- Fast and Fair
    'fast_fair': [
        'FAST_FAIR/squint-config.ini',
    ],
    # -- Level Hashing
    'level_hashing': [
        'Level_Hashing/squint-config.ini',
    ],
    # -- WOART
    'woart': [
        'WORT/woart/squint-config.ini',
    ],
    # -- WORT
    'wort': [
        'WORT/wort/squint-config.ini',
    ],

    # RECIPE Indices
    # -- P-ART
    'p_art': [
        'recipe_part/squint-config-small.ini',
        'recipe_part/squint-config.ini',
    ],
    # -- P-BwTree
    'p_bwtree': [
        'recipe_pbwtree/squint-config.ini',
    ],
    # -- P-CLHT
    'p_clht': [
        'recipe_pclht_witcher/squint-config.ini',
        'recipe_pclht_witcher_aga/squint-config.ini',
        'recipe_pclht_witcher_aga_tx/squint-config.ini',
    ],
    # -- P-HOT
    'p_hot': [
        'recipe_phot/squint-config.ini',
    ],
    # -- P-Masstree
    'p_masstree': [
        'recipe_pmasstree/squint-config.ini',
    ],

    # Server Applications
    # -- HSE
    # 'hse_v1': [
    #     'hse-project/squint-config.ini',
    # ],
    'hse_v2': [
        'hse-project/squint-config-new-kvdb.ini',
    ],
    # -- Memcached
    'memcached': [
        'memcached-pm/squint-config.ini',
        'memcached-pm/squint-config-witcher.ini',
    ],
    # -- Redis
    'redis': [
        'redis-3.2-nvml/squint-config.ini',
        'redis-3.2-nvml/squint-config-witcher.ini',
    ],
}

SQUINT_SMALL_TARGETS = {
    # PMDK Data Structures
    # -- Array
    'pmdk_array': [
        'pmdk_array_v1.4/squint-config.ini',
        'pmdk_array_v1.4/squint-config-free.ini',
        'pmdk_array_v1.4/squint-config-realloc.ini',
        'pmdk_array_v1.4/squint-config-toid.ini',

        'pmdk_array_v1.8/squint-config.ini',
        'pmdk_array_v1.8/squint-config-free.ini',
        'pmdk_array_v1.8/squint-config-realloc.ini',
        'pmdk_array_v1.8/squint-config-toid.ini',
    ],
    # -- Btree
    'pmdk_btree': [
        'pmdk_btree_jaaru/squint-config.ini',
        'pmdk_btree_map_v1.4/squint-config.ini',
        'pmdk_btree_map_v1.8/squint-config.ini',
    ],
    # -- Ctree
    'pmdk_ctree': [
        'pmdk_ctree_jaaru/squint-config.ini',
        'pmdk_ctree_map_v1.4/squint-config.ini',
        'pmdk_ctree_map_v1.8/squint-config.ini',
    ],
    # -- Hashmap Atomic
    'pmdk_hashmap_atomic': [
        'pmdk_hashmap_atomic_jaaru/squint-config.ini',
        'pmdk_hashmap_atomic_v1.4/squint-config.ini',
        'pmdk_hashmap_atomic_v1.8/squint-config.ini',
    ],
    # -- Hashmap TX
    'pmdk_hashmap_tx': [
        'pmdk_hashmap_tx_jaaru/squint-config.ini',
        'pmdk_hashmap_tx_v1.4/squint-config.ini',
        'pmdk_hashmap_tx_v1.8/squint-config.ini',
    ],
    # -- Rbtree
    'pmdk_rbtree': [
        'pmdk_rbtree_jaaru/squint-config.ini',
        'pmdk_rbtree_map_v1.4/squint-config.ini',
        'pmdk_rbtree_map_v1.4/squint-config-freebug.ini',
        'pmdk_rbtree_map_v1.8/squint-config.ini',
    ],

    # Key-Value Indices
    # -- CCEH
    'cceh': [
        'CCEH/squint-config-small.ini',
    ],
    # -- Fast and Fair
    'fast_fair': [
        'FAST_FAIR/squint-config-small.ini',
    ],
    # -- Level Hashing
    'level_hashing': [
        'Level_Hashing/squint-config-small.ini',
    ],
    # -- WOART
    'woart': [
        'WORT/woart/squint-config-small.ini',
    ],
    # -- WORT
    'wort': [
        'WORT/wort/squint-config-small.ini',
    ],

    # RECIPE Indices
    # -- P-ART
    'p_art': [
        'recipe_part/squint-config-small.ini',
    ],
    # -- P-BwTree
    'p_bwtree': [
        'recipe_pbwtree/squint-config-small.ini',
    ],
    # -- P-CLHT
    'p_clht': [
        'recipe_pclht_witcher/squint-config-small.ini',
        'recipe_pclht_witcher_aga/squint-config-small.ini',
        'recipe_pclht_witcher_aga_tx/squint-config-small.ini',
    ],
    # -- P-HOT
    'p_hot': [
        'recipe_phot/squint-config-small.ini',
    ],
    # -- P-Masstree
    'p_masstree': [
        'recipe_pmasstree/squint-config-small.ini',
    ],

    # Server Applications
    # -- HSE
        # 'hse_v1': [
        #     'hse-project/squint-config.ini',
        # ],
        # 'hse_v2': [
        #     'hse-project/squint-config-new-kvdb.ini',
        # ],
    # -- Memcached
        # 'memcached': [
        #     'memcached-pm/squint-config.ini',
        # ],
    # -- Redis
        # 'redis': [
        #     'redis-3.2-nvml/squint-config.ini',
        # ],
}

BASELINE_FULL_TARGETS = {}

JAARU_FULL_TARGETS = {}

TARGET_GROUPS = {
    'pmdk' : [
        'pmdk_array',
        'pmdk_btree',
        'pmdk_ctree',
        'pmdk_hashmap_atomic',
        'pmdk_hashmap_tx',
        'pmdk_rbtree',
    ],
    'kvi': [
        'cceh',
        'fast_fair',
        'level_hashing',
        'woart',
        'wort',
    ],
    'recipe': [
        'p_art',
        'p_bwtree',
        'p_clht',
        'p_hot',
        'p_masstree',
    ],
    'server': [
        # 'hse_v1',
        'hse_v2',
        'memcached',
        'redis',
    ],
    'server_posix': [
        'leveldb',
        'rocksdb',
        'wiredtiger',
    ],
}


def available_target_sets():
    return {
        'squint_full': SQUINT_FULL_TARGETS,
        'squint_small': SQUINT_SMALL_TARGETS,
        'baseline_full': BASELINE_FULL_TARGETS,
        'jaaru_full': JAARU_FULL_TARGETS,
    }

def get_target_set(target_name):
    targets = available_target_sets()
    target = list(filter(lambda l: target_name in l, targets))
    if len(target) > 1:
        raise Exception(f'"{target_name}" too broad, matched multiple target sets: {target}')
    elif not target:
        raise Exception(f'"{target_name}" did not match any target sets.')

    return targets[target[0]]

def _validate_config():
    '''
    Validates that we configured everything properly.
    '''
    bugs = get_bugs()
    for target_set_name, target_config in available_target_sets().items():
        for bug_set in target_config:
            assert bug_set in bugs, f'No configuration for bugs in {target_set_name} {bug_set}!'

_validate_config()