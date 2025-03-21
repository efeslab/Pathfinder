'''
This file contains the list of diagnosed bugs and their locations.

Even though we have different versions of the same target, we want have all those
targets in the same group, because different versions may have overlapping bugs.
'''


# for each target: contains a map from the bugs name to a list of locations
BUG_LOCATIONS = {
    # PMDK
    # -- Array
    "pmdk_array": {
        "0": [
            # Bug: non-atomic allocations
            # atomic 
            
            "targets/pmdk_array_v1.4/array.c:477",
            "targets/pmdk_array_v1.4/array.c:479",
            "targets/pmdk_array_v1.4/array.c:480",
            "targets/pmdk_array_v1.8/array.c:477",
            "targets/pmdk_array_v1.8/array.c:479",
            "targets/pmdk_array_v1.8/array.c:480",
            "obj.c:2329",
        ],
        "1": [
            # Bug: PMEMoid array alloc interrupted
            # This would be an ordering bug.
            
            "targets/pmdk_array_v1.4/array.c:337",
            "targets/pmdk_array_v1.4/array.c:345",
            "targets/pmdk_array_v1.4/array.c:481",
            "targets/pmdk_array_v1.4/array.c:482",
            "targets/pmdk_array_v1.4/array.c:483",
            "targets/pmdk_array_v1.8/array.c:345",
            "targets/pmdk_array_v1.8/array.c:337",
            "targets/pmdk_array_v1.8/array.c:481",
            "targets/pmdk_array_v1.8/array.c:482",
            "targets/pmdk_array_v1.8/array.c:483",
        ],
        "2": [
            # Bug: realloc
            # Also an ordering issue
            
            "targets/pmdk_array_v1.4/array.c:454",
            "targets/pmdk_array_v1.4/array.c:449",
            "targets/pmdk_array_v1.8/array.c:454",
            "targets/pmdk_array_v1.8/array.c:449",
        ],
        "3": [
            # Bug: free pmemoid
            # Also an ordering thing
            
            "targets/pmdk_array_v1.4/array.c:207",
            "targets/pmdk_array_v1.8/array.c:207",
        ],
        "4": [
            # Bug: non-atomic free
            # Atomicity bug
            
            "targets/pmdk_array_v1.4/array.c:426",
            "targets/pmdk_array_v1.4/array.c:427",
            "targets/pmdk_array_v1.8/array.c:426",
            "targets/pmdk_array_v1.8/array.c:427",
        ],
        "5": [
            # Bug: realloc TOID
            # ordering bug
            
            "targets/pmdk_array_v1.4/array.c:225",
            "targets/pmdk_array_v1.8/array.c:225",
        ],
        "6": [
            # Bug: realloc PMEMoid
            # ordering bug
            
            "targets/pmdk_array_v1.4/array.c:260",
            "targets/pmdk_array_v1.8/array.c:260",
        ],
    },
    # -- Btree
    "pmdk_btree": {
        "0": [
            # This is the libpmemobj bug
            # transaction violation
            
            "tx.c:615",
            "targets/pmdk_btree_map_v1.4/driver.c:82",
            "targets/pmdk_btree_map_v1.4/driver.c:83"
        ],
        "1": [
            # Bug: bad update
            # Lack of atomicity in deletion
            
            "targets/pmdk_btree_map_v1.4/driver.c:90",
            "targets/pmdk_btree_map_v1.4/driver.c:85",
            "targets/pmdk_btree_map_v1.4/driver.c:91",
            "targets/pmdk_btree_map_v1.4/driver.c:86",
            "targets/pmdk_btree_map_v1.4/driver.c:87",
            "targets/pmdk_btree_map_v1.8/driver.c:92",
            # "targets/pmdk_btree_jaaru/driver.c:148",
        ],
        "2": [
            # Bug: bad removal
            # Lack of atomicity in deletion
            
            "targets/pmdk_btree_map_v1.4/driver.c:92",
            "targets/pmdk_btree_map_v1.4/btree_map.c:438",
            "targets/pmdk_btree_map_v1.4/driver.c:120",
            "targets/pmdk_btree_map_v1.4/driver.c:121",
            "targets/pmdk_btree_map_v1.4/driver.c:122",
            "targets/pmdk_btree_map_v1.4/btree_map.c:375",
            "targets/pmdk_btree_map_v1.4/btree_map.c:540",
            "targets/pmdk_btree_map_v1.4/btree_map.c:519",
            "targets/pmdk_btree_map_v1.4/btree_map.c:364",
            "targets/pmdk_btree_map_v1.8/driver.c:125",
            "targets/pmdk_btree_map_v1.8/driver.c:126",
            "targets/pmdk_btree_map_v1.8/driver.c:127",
            "targets/pmdk_btree_map_v1.8/btree_map.c:537",
            "targets/pmdk_btree_map_v1.8/btree_map.c:524",
            "targets/pmdk_btree_map_v1.8/btree_map.c:516",
        ],
        "3": [
            # Bug: crash during POBJ_ROOT call (init bug)
            # Atomic allocation issue
            
            "obj.c:2941"
        ]
    },
    # -- Ctree
    "pmdk_ctree": {
        "0": [
            # Bug: interrupted pmemobj_root call in v1.4 (transaction error)
            # https://github.com/uci-plrg/jaaru-pmdk/blob/667a470d39e69f59b70bbbd9351590cf1464e104/src/examples/libpmemobj/map/data_store.c#L172
            
            "obj.c:2941",
        ],
    },
    # -- Hashmap Atomic
    "pmdk_hashmap_atomic": {
        "0": [
            # Bug: interrupted hashmap creation
            # Non-atomic creationg
            
            "targets/pmdk_hashmap_atomic_v1.4/hashmap_atomic.c:132",
            "targets/pmdk_hashmap_atomic_v1.4/hashmap_atomic.c:135",
            "targets/pmdk_hashmap_atomic_v1.4/hashmap_atomic.c:138",
            "targets/pmdk_hashmap_atomic_v1.4/hashmap_atomic.c:137",
            "targets/pmdk_hashmap_atomic_v1.4/hashmap_atomic.c:144",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:132",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:135",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:138",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:137",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:144",
            "targets/pmdk_hashmap_atomic_jaaru/hashmap_atomic.c:130",
            "targets/pmdk_hashmap_atomic_jaaru/hashmap_atomic.c:131",
            "targets/pmdk_hashmap_atomic_jaaru/hashmap_atomic.c:132",
            "targets/pmdk_hashmap_atomic_jaaru/hashmap_atomic.c:133",
            "targets/pmdk_hashmap_atomic_jaaru/hashmap_atomic.c:139",
            "targets/pmdk_hashmap_atomic_jaaru/hashmap_atomic.c:144",
        ],
        "1": [
            # Bug: initialization interruption results in a segfault.
            # create buckets
            # Non-atomic allocation
            
            "targets/pmdk_hashmap_atomic_v1.4/hashmap_atomic.c:442",
            "targets/pmdk_hashmap_atomic_v1.4/hashmap_atomic.c:118",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:441",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:118",
            "targets/pmdk_hashmap_atomic_jaaru/hashmap_atomic.c:436",
            "targets/pmdk_hashmap_atomic_jaaru/hashmap_atomic.c:116",
        ],
        "2": [
            # Bug: rebuild segfault (non-atomic reallocation)
            
            "targets/pmdk_hashmap_atomic_v1.4/hashmap_atomic.c:196",
            "targets/pmdk_hashmap_atomic_v1.4/hashmap_atomic.c:198",
            "targets/pmdk_hashmap_atomic_v1.4/hashmap_atomic.c:227",
            "targets/pmdk_hashmap_atomic_v1.4/hashmap_atomic.c:237",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:196",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:198",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:227",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:237",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:319",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:330",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:334",
            "obj.c:3258",
            "targets/pmdk_hashmap_atomic_jaaru/hashmap_atomic.c:191",
            "targets/pmdk_hashmap_atomic_jaaru/hashmap_atomic.c:193",
        ],
        "3": [
            # Bug: non-atomic insert results in value and key not initialized
            # also here: https://github.com/uci-plrg/jaaru-pmdk/blob/667a470d39e69f59b70bbbd9351590cf1464e104/src/examples/libpmemobj/map/data_store.c#L78
            
            "targets/pmdk_hashmap_atomic_v1.8/driver.c:71",
            "targets/pmdk_hashmap_atomic_v1.8/driver.c:72",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:263",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:280",
            "targets/pmdk_hashmap_atomic_v1.8/hashmap_atomic.c:284",
            "obj.c:3231",
        ]
    },
    "pmdk_hashmap_tx": {
        "0": [
            # Bug: Even though TX new is used, the pointer value isn't backed
            # up. So, the data will be revoked, but the pointer isn't reverted.
            # Since the map isn't backed up before the transaction at
            # main.c:159, the recovery code will read old data in the checker.
            
            "targets/pmdk_hashmap_tx_jaaru/driver.c:164",
            "targets/pmdk_hashmap_tx_jaaru/hashmap_tx.c:402",
            "targets/pmdk_hashmap_tx_jaaru/hashmap_tx.c:405",
        ],
        "1": [
            # Bug: the key and value are not persisted, so when the transaction
            # completes, the key and value may not persist. It's an undo log,
            # not also a redo log. In insert
            
            "targets/pmdk_hashmap_tx_jaaru/driver.c:74",
            "targets/pmdk_hashmap_tx_jaaru/driver.c:75",
            "targets/pmdk_hashmap_tx_jaaru/hashmap_tx.c:99",
            "targets/pmdk_hashmap_tx_jaaru/hashmap_tx.c:100",
            "targets/pmdk_hashmap_tx_jaaru/hashmap_tx.c:144",
            "targets/pmdk_hashmap_tx_jaaru/hashmap_tx.c:206",
            "targets/pmdk_hashmap_tx_jaaru/hashmap_tx.c:207",
            "targets/pmdk_hashmap_tx_jaaru/hashmap_tx.c:208",
            "targets/pmdk_hashmap_tx_jaaru/hashmap_tx.c:209",
            "targets/pmdk_hashmap_tx_jaaru/hashmap_tx.c:210",
            "targets/pmdk_hashmap_tx_jaaru/hashmap_tx.c:212",
            "tx.c:210",
            "memops.c:702",
            "ulog.c:286",
            "targets/pmdk_hashmap_tx_v1.4/driver.c:71",
            "targets/pmdk_hashmap_tx_v1.4/driver.c:72",
            "targets/pmdk_hashmap_tx_v1.4/hashmap_tx.c:204",
            "targets/pmdk_hashmap_tx_v1.4/hashmap_tx.c:207",
            "targets/pmdk_hashmap_tx_v1.4/hashmap_tx.c:208",
            "targets/pmdk_hashmap_tx_v1.4/hashmap_tx.c:209",
            "targets/pmdk_hashmap_tx_v1.4/hashmap_tx.c:210",
            "targets/pmdk_hashmap_tx_v1.4/hashmap_tx.c:211",
            "targets/pmdk_hashmap_tx_v1.4/hashmap_tx.c:213",
        ]
    },
    "pmdk_rbtree": {
        "0": [
            # Bug: unpersisted write (not added to pmdk transaction)
            
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:208"
        ],
        "1": [
            # Bug: unpersisted write (parent modification not added to transaction)
            
            "targets/pmdk_rbtree_map_v1.4/driver.c:95",
            "targets/pmdk_rbtree_map_v1.4/driver.c:96",
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:384",
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:415",
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:419",
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:420",
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:425",
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:429",
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:430",
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:431",
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:432",
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:433",
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:434",
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:436",
        ],
        "2": [
            # Bug: unpersisted write (parent modification not added to transaction)
            
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:437"
        ],
        "3": [
             # Bug: free bug (free is non-transactional, when it should be transactional, so a crash
             # causes other operations to be reverted, but not the free operation)
             
            "targets/pmdk_rbtree_map_v1.4/rbtree_map.c:557"
        ]
    },
    # Key-Value Indices
    "cceh": {
        "0": [
            # Bug: non-atomic initialization
            
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:525",
            "obj.c:2941",
            "obj.c:2916",
        ],
        "1": [
            # Bug: non-atomic initialization
            
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:532",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:534",
            "targets/CCEH/CCEH/src/pmdk.c:116",
        ],
        "2": [
            # Bug: in insert (non atomic insert)
            
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:218",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:219",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:220",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:233",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:241",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:244",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:301",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:304",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:305",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:307",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:308",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:313",
        ],
        "3": [
            # Bug: segment insert (non-atomic insert)
            
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:26",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:41",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:47",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:50",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:52",
            "targets/CCEH/CCEH/src/CCEH_MSB.cpp:60",
            "targets/CCEH/CCEH/main/main.cpp:36",
            "targets/CCEH/CCEH/main/main.cpp:48",
            "targets/CCEH/CCEH/main/main.cpp:61",
        ]
    },
    "fast_fair": {
        "0": [
            # Bug: remove_key (non-atomic)
            
            "targets/FAST_FAIR/include/btree.h:224",
            "targets/FAST_FAIR/include/btree.h:236",
            "targets/FAST_FAIR/include/btree.h:237",
        ],
        "1": [
            # Bug: remove_key (non-atomic)
            
            "targets/FAST_FAIR/include/btree.h:230",
            "targets/FAST_FAIR/include/btree.h:252",
        ],
        "2": [
            # Bug: non-atomic insertion
            
            "targets/FAST_FAIR/include/btree.h:546",
            "targets/FAST_FAIR/include/btree.h:556",
            "targets/FAST_FAIR/include/btree.h:567"
        ],
        "3": [
            # bug: page::remove (non-atomic)
            
            "targets/FAST_FAIR/include/btree.h:320",
            "targets/FAST_FAIR/include/btree.h:324",
            "targets/FAST_FAIR/include/btree.h:327",
            "targets/FAST_FAIR/include/btree.h:367",
            "targets/FAST_FAIR/include/btree.h:374",
            "targets/FAST_FAIR/include/btree.h:379",
            "targets/FAST_FAIR/include/btree.h:385",
            "targets/FAST_FAIR/include/btree.h:420",
            "targets/FAST_FAIR/include/btree.h:430",
            "targets/FAST_FAIR/include/btree.h:433",
        ],
        "4": [
            # Bug: non atomic initialization
            
            "targets/FAST_FAIR/include/btree.h:1061",
            "obj.c:2941",
            "obj.c:2916",
            "targets/FAST_FAIR/lib/pmdk.cpp:46",
            "targets/FAST_FAIR/main/main.cpp:25"
        ],
        "5": [
            # Bug: non-atomic initialization of data structure
            
            "targets/FAST_FAIR/include/btree.h:1066",
            "targets/FAST_FAIR/include/btree.h:1068"
        ],
        "6": [
            # Bug: page::store
            # non-atomic insertion
            
            "targets/FAST_FAIR/include/btree.h:532",
            "targets/FAST_FAIR/include/btree.h:559",
            "targets/FAST_FAIR/include/btree.h:564",
            "targets/FAST_FAIR/include/btree.h:566",
            "targets/FAST_FAIR/include/btree.h:570",
            "targets/FAST_FAIR/include/btree.h:583",
        ]
    },
    "level_hashing": {
        "0": [
            # Bug: non-atomic initialization
            
            "targets/Level_Hashing/lib/level_hashing.c:66",
            "targets/Level_Hashing/lib/level_hashing.c:69",
        ],
        "1": [
            # Bug: non-atomic commit
            
            "targets/Level_Hashing/lib/level_hashing.c:80",
            "targets/Level_Hashing/lib/level_hashing.c:81",
            "targets/Level_Hashing/lib/level_hashing.c:82",
            "targets/Level_Hashing/lib/level_hashing.c:83",
            "targets/Level_Hashing/lib/level_hashing.c:84",
            "targets/Level_Hashing/lib/level_hashing.c:85",
            "targets/Level_Hashing/lib/level_hashing.c:86",
            "targets/Level_Hashing/lib/level_hashing.c:87",
            "targets/Level_Hashing/lib/level_hashing.c:88",
            "targets/Level_Hashing/lib/level_hashing.c:89",
            "targets/Level_Hashing/lib/level_hashing.c:90",
            "targets/Level_Hashing/lib/level_hashing.c:98",
            "targets/Level_Hashing/lib/level_hashing.c:121",
            "obj.c:2941",
            "obj.c:2916",
            "targets/Level_Hashing/lib/pmdk.c:46",
        ],
        "2": [
            # Bug: unpersisted metadata in level_expand
            
            "targets/Level_Hashing/lib/level_hashing.c:213",
            "targets/Level_Hashing/lib/level_hashing.c:214",
            "targets/Level_Hashing/lib/level_hashing.c:215",
            "targets/Level_Hashing/lib/level_hashing.c:217",
            "targets/Level_Hashing/lib/level_hashing.c:218",
            "targets/Level_Hashing/lib/level_hashing.c:219",
            "targets/Level_Hashing/lib/level_hashing.c:221",
            "targets/Level_Hashing/lib/level_hashing.c:222",
            "targets/Level_Hashing/lib/level_hashing.c:223",
            "targets/Level_Hashing/lib/level_hashing.c:229",
        ],
        "3": [
            # Bug: unpersisted metadata in level_shrink
            
            "targets/Level_Hashing/lib/level_hashing.c:227",
            "targets/Level_Hashing/lib/level_hashing.c:257",
            "targets/Level_Hashing/lib/level_hashing.c:260",
            "targets/Level_Hashing/lib/level_hashing.c:262",
            "targets/Level_Hashing/lib/level_hashing.c:263",
            "targets/Level_Hashing/lib/level_hashing.c:264",
        ],
        "4": [
            # Bug: unpersisted metadata in level_shrink
            
            "targets/Level_Hashing/lib/level_hashing.c:268",
            "targets/Level_Hashing/lib/level_hashing.c:271",
            "targets/Level_Hashing/lib/level_hashing.c:272",
            "targets/Level_Hashing/lib/level_hashing.c:273",
        ],
        "5": [
            # Bug: unpersisted metadata update
            
            "targets/Level_Hashing/lib/level_hashing.c:407",
            "targets/Level_Hashing/lib/level_hashing.c:409",
            "targets/Level_Hashing/lib/level_hashing.c:415",
        ],
        "6": [
            # Bug: unpersisted metadata update
            
            "targets/Level_Hashing/lib/level_hashing.c:417",
            "targets/Level_Hashing/lib/level_hashing.c:419",
            "targets/Level_Hashing/lib/level_hashing.c:425",
        ],
        "7": [
            # Bug: level_insert
            # Bug: non-atomic insert
            
            "targets/Level_Hashing/lib/level_hashing.c:529",
            "targets/Level_Hashing/lib/level_hashing.c:530",
            "targets/Level_Hashing/lib/level_hashing.c:544",
            "targets/Level_Hashing/lib/level_hashing.c:545",
            "targets/Level_Hashing/lib/level_hashing.c:546",
        ],
        "8": [
            # Bug: non-atomic insert
            
            "targets/Level_Hashing/lib/level_hashing.c:558",
            "targets/Level_Hashing/lib/level_hashing.c:538"
        ],
        "9": [
            # Bug: non-atomic insert
            
            "targets/Level_Hashing/lib/level_hashing.c:594",
            "targets/Level_Hashing/lib/level_hashing.c:595",
            "targets/Level_Hashing/lib/level_hashing.c:597"
        ],
        "10": [
            # Bug: non-atomic insert
            
            "targets/Level_Hashing/lib/level_hashing.c:609",
            "targets/Level_Hashing/lib/level_hashing.c:638"
        ],
        "11": [
            # Bug: entry update error in b2t_movement
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:692",
            "targets/Level_Hashing/lib/level_hashing.c:693",
            "targets/Level_Hashing/lib/level_hashing.c:694",
        ],
        "12": [
            # Bug: entry update in b2t_movement
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:712",
            "targets/Level_Hashing/lib/level_hashing.c:713",
            "targets/Level_Hashing/lib/level_hashing.c:714",
        ],
        "13": [
            # Bug: b2t_movement
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:727",
            "targets/Level_Hashing/lib/level_hashing.c:732",
        ],
        "14": [
            # Bug: non-atomic insert
            
            "targets/Level_Hashing/lib/level_hashing.c:537",
            "targets/Level_Hashing/lib/level_hashing.c:531"
        ],
        "15": [
            # Bug: non-atomic insert
            
            "targets/Level_Hashing/lib/level_hashing.c:552"
        ],
        "16": [
            # Bug: level update
            # non-atomic update
            
            "targets/Level_Hashing/lib/level_hashing.c:451",
            "targets/Level_Hashing/lib/level_hashing.c:453",
            "targets/Level_Hashing/lib/level_hashing.c:460",
            "targets/Level_Hashing/lib/level_hashing.c:464",
            "targets/Level_Hashing/lib/level_hashing.c:466",
            "targets/Level_Hashing/lib/level_hashing.c:469",
        ],
        "17": [
            # Bug: try_movement
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:644",
            "targets/Level_Hashing/lib/level_hashing.c:646",
        ],
        "18": [
            # Bug: try_movement
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:659",
            "targets/Level_Hashing/lib/level_hashing.c:652",
        ],
        "19": [
            # Bug: b2t_movement
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:700",
            "targets/Level_Hashing/lib/level_hashing.c:702",
        ],
        "20": [
            # Bug: b2t_movement
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:720",
            "targets/Level_Hashing/lib/level_hashing.c:722",
        ],
        "21": [
            # Bug: level_insert entry update after movement
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:580",
            "targets/Level_Hashing/lib/level_hashing.c:581",
            "targets/Level_Hashing/lib/level_hashing.c:582",
            "targets/Level_Hashing/lib/level_hashing.c:588",
        ],
        "22": [
            # Bug: level_insert metadata update
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:603",
        ],
        "23": [
            # Bug: level_update
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:450",
            "targets/Level_Hashing/lib/level_hashing.c:478",
            "targets/Level_Hashing/lib/level_hashing.c:479",
            "targets/Level_Hashing/lib/level_hashing.c:481",
            "targets/Level_Hashing/lib/level_hashing.c:488",
            "targets/Level_Hashing/lib/level_hashing.c:492",
            "targets/Level_Hashing/lib/level_hashing.c:494",
        ],
        "24": [
            # Bug: metadata in level_expand
            # non-atomic resize
            
            "targets/Level_Hashing/lib/level_hashing.c:140",
            "targets/Level_Hashing/lib/level_hashing.c:143",
            "targets/Level_Hashing/lib/level_hashing.c:144",
            "targets/Level_Hashing/lib/level_hashing.c:149",
            "targets/Level_Hashing/lib/level_hashing.c:206",
        ],
        "25": [
            # Bug: level_shrink
            # non-atomic metadata swapping
            
            "targets/Level_Hashing/lib/level_hashing.c:267",
            "targets/Level_Hashing/lib/level_hashing.c:270",
            "targets/Level_Hashing/lib/level_hashing.c:288",
            "targets/Level_Hashing/lib/level_hashing.c:295",
            "targets/Level_Hashing/lib/level_hashing.c:296",
        ],
        "26": [
            # Bug: entry updates in try_movement
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:637",
        ],
        "27": [
            # Bug: metadata in b2t_movement
            # metadata is just unpersisted
            
            "targets/Level_Hashing/lib/level_hashing.c:706",
            "targets/Level_Hashing/lib/level_hashing.c:707",
        ],
        "28": [
            # Bug: non-persisted metadata update on number of items
            
            "targets/Level_Hashing/lib/level_hashing.c:726",
            "targets/Level_Hashing/lib/level_hashing.c:728",
        ],
        "29": [
            # Bug: non-atomic data movement
            
            "targets/Level_Hashing/lib/level_hashing.c:454",
            "targets/Level_Hashing/lib/level_hashing.c:459",
        ],
        "30": [
            # Bug: non-atomic data movement
            
            "targets/Level_Hashing/lib/level_hashing.c:482",
            "targets/Level_Hashing/lib/level_hashing.c:487",
        ],
        "31": [
            # Bug: bad entry update in level_expand
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:171",
            "targets/Level_Hashing/lib/level_hashing.c:172",
            "targets/Level_Hashing/lib/level_hashing.c:173",
        ],
        "32": [
            # Bug: bad entry update in level_expand (second if)
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:186",
            "targets/Level_Hashing/lib/level_hashing.c:187",
            "targets/Level_Hashing/lib/level_hashing.c:188",
        ],
        "33": [
            # Bug: level_insert entry update after movement (second if)
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:596",
        ],
        "34": [
            # Bug: entry updates in try_movement (second if)
            # invalid persistence ordering (data not persisted before token updated)
            
            "targets/Level_Hashing/lib/level_hashing.c:651",
            "targets/Level_Hashing/lib/level_hashing.c:652",
            "targets/Level_Hashing/lib/level_hashing.c:653",
        ],
    },
    "woart": {
        "0": [
            # Bug: art insert is not atomic
            
            "targets/WORT/woart/lib/woart.c:533",
            "targets/WORT/woart/lib/woart.c:730",
            "targets/WORT/woart/lib/woart.c:737",
            "targets/WORT/woart/lib/woart.c:738",
            "targets/WORT/woart/lib/woart.c:776",
        ],
        "1": [
            # Bug: non-atomic initialization
            
            "targets/WORT/woart/lib/woart.c:788",
            "targets/WORT/woart/lib/woart.c:785"
        ],
        
        "2": [
            # Bug: non-atomic initialization
            
            "targets/WORT/woart/lib/woart.c:790",
            "targets/WORT/woart/lib/woart.c:791",
            "obj.c:2941",
            "obj.c:2916",
            "obj.c:2253",
        
            # Combined with the previous 
            
            "targets/WORT/woart/lib/woart.c:794",
            "targets/WORT/woart/lib/woart.c:374"
        ]
    },
    "wort": {
        "0": [
            # Bug: non-atomic initialization
             
            "targets/WORT/wort/lib/wort.c:103",
            "obj.c:2941",
            "obj.c:2916",
            "obj.c:2253",
            "targets/WORT/wort/main/main.c:22"
        ],
        "1": [
            # Bug: non-atomic initialization
            
            "targets/WORT/wort/lib/wort.c:109",
            "targets/WORT/wort/lib/wort.c:113",
        ]
    },
    # RECIPE
    "p_art": {
        "0": [
            # Bug: remove is non-atomic
            
            "targets/recipe_part/src/Tree.cpp:463",
            "targets/recipe_part/src/Tree.cpp:467",
            "targets/recipe_part/src/Tree.cpp:477",
            "targets/recipe_part/src/Tree.cpp:484",
            "targets/recipe_part/src/Tree.cpp:486",
            "targets/recipe_part/src/Tree.cpp:487",
            "targets/recipe_part/src/Tree.cpp:491",
            "targets/recipe_part/src/Tree.cpp:496",
            "targets/recipe_part/src/Tree.cpp:504",
            "targets/recipe_part/src/Tree.cpp:535",
        ],
        "1": [
            # Bug: make leaf/insert is non-atomic
            
            "targets/recipe_part/src/Key.h:39",
            "targets/recipe_part/src/Key.h:43",
            "targets/recipe_part/src/Key.h:44",
            "targets/recipe_part/src/Key.h:45",
            "targets/recipe_part/src/N4.cpp:71",
            "targets/recipe_part/src/N4.cpp:72",
            "targets/recipe_part/src/N4.cpp:73",
            "targets/recipe_part/src/N4.cpp:74",
            "targets/recipe_part/src/Tree.cpp:386",
            "targets/recipe_part/src/Tree.cpp:389",
            "targets/recipe_part/src/Tree.cpp:413",
            "targets/recipe_part/src/Tree.cpp:418",
            "atomic_base.h:464",
        ],
        "2": [
            # Bug: init_P_ART (non-atomic init as well)
            
            "targets/recipe_part/src/Tree.cpp:70",
            "obj.c:2916",
        ],
        "3": [
            # Bug: init_P_ART (non-atomic init)
            
            "targets/recipe_part/src/Tree.cpp:26",
            "targets/recipe_part/src/Tree.cpp:77",
            "targets/recipe_part/src/Tree.cpp:79",
            "targets/recipe_part/src/pmdk.cpp:65",
            "targets/recipe_part/src/pmdk.cpp:110",
        ]
    },
    "p_bwtree": {
        # "0": [
        #     "targets/recipe_pbwtree/include/bwtree.h:2024",
        # ],
        "0": [
            # Bug: Non-atomic data initialization
            
            "targets/recipe_pbwtree/include/bwtree.h:2391",
        ],
        "1": [
            # Bug: non-atomic/non-recovered initialization
            
            "targets/recipe_pbwtree/main/main.cpp:78",
            "obj.c:2941",
            "obj.c:2916",
        ],
        "2": [
            # Bug: non-atomic/non-recovered initialization
            
            "targets/recipe_pbwtree/main/main.cpp:85",
            "targets/recipe_pbwtree/main/main.cpp:87",
            "targets/recipe_pbwtree/lib/pmdk.cpp:65",
            'targets/recipe_pbwtree/include/bwtree.h:3329',
        ]
    },
    "p_clht": {
        "0": [
            # Bug: non-atomic resize
            
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:680",
        ],
        "1": [
            # Bug: non-atomic initialization
            
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:1060",
        ],
        "2": [
            # Bug: non-atomic clht_put
            
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:392",
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:430",
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:431",
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:436",
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:442",
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:447",
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:452",
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:456",
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:461",
            "targets/recipe_pclht_witcher/include/clht_lb_res.h:304",
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:511",
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:513",
        ],
        "3": [
            # Bug: non-atomic / non-recovered init
            
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:1067",
            "obj.c:2253",
            "obj.c:2916",
            "targets/recipe_pclht_witcher/lib/clht_lb_res.c:1071",
        ],

        # Agamotto, non-TX
        "4": [
            # Bug: resize is performed non-atomically
            
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:841",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:851",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:886",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:941",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:975",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:976",
        ],
        "5": [
            # Bug: non-persisted updates
            
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:566",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:567",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:572",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:578",
        
            # Combined with previous - was 6
            
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:585",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:590",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:599",
        ],
        "7": [
            # Bug: non-atomic initialization
            
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:269",
        ],
        "8": [
            # Bug: non-atomic creation
            
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:298",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:303",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:311",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:312",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:313",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:314",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:315",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:316",
        
            # Combined with previous - was 10 - overlap
            
            "targets/recipe_pclht_witcher_aga/include/clht_lb_res.h:311",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:594",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:656",
            "targets/recipe_pclht_witcher_aga/lib/clht_lb_res.c:659",
        ],

        # Agamotto TX
        "10": [
            # Updates are not properly added to the transaction. Lack of persistence.
        
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:544",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:546",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:547",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:552",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:557",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:559",
        ],
        "11": [
            # Bug: clht_put. Updates are not properly added to the transaction. Lack of persistence.
            
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:212",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:585",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:590",
        ],
        "12": [
            # Bug: non-atomic / non-recovered init
            
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:298",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:345",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:367",
        ],
        "13": [
            # Bug: needs to recreate pool on restart
            
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:269",
        ],
        "14": [
            # Bug: persistent lock
            
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:594",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:651",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:652",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:654",
            "targets/recipe_pclht_witcher_aga_tx/lib/clht_lb_res_tx.c:659",
            "targets/recipe_pclht_witcher_aga_tx/include/clht_lb_res.h:311",
        ]
    },
    "p_hot": {
        "0": [
            # Non-atomic insertion of the node
            
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowexNode.hpp:351",
            'targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowexNode.hpp:357',
        ],
        "1": [
            # Non-atomic pointer update
            
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowex.hpp:281",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowex.hpp:282",

            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowexNode.hpp:263",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowexNode.hpp:275",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowexNode.hpp:286",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowexNode.hpp:291",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowexNode.hpp:295",

            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowexNode.hpp:144",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowex.hpp:161",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowex.hpp:164",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowex.hpp:167",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowex.hpp:196",

            "targets/recipe_phot/main/main.cpp:40",
            "targets/recipe_phot/main/main.cpp:41",
            "targets/recipe_phot/main/main.cpp:42",
        ],
        "2": [
            # Non-atomic init
            
            'targets/recipe_phot/main/main.cpp:288',
        ],
        "3": [
            # Init non-atomicity
            
            'targets/recipe_phot/main/main.cpp:295',
            'targets/recipe_phot/main/main.cpp:297',
            "obj.c:2916",
            "targets/recipe_phot/include/hot/commons/include/pmdk.h:63",
        ],
        "4": [
             # Insertion non-atomicity
        
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowex.hpp:122",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowex.hpp:130",
        ],
        "5": [
            # Bug: persistent lock
            # lock is persisted, blocking the data structure on restart.
            
            "targets/recipe_phot/include/hot/commons/include/pmdk.h:91",
            "targets/recipe_phot/include/hot/commons/include/pmdk.h:106",
            "targets/recipe_phot/main/main.cpp:55",
            "targets/recipe_phot/main/main.cpp:56",
            "targets/recipe_phot/main/main.cpp:57",
            "atomic_base.h:464",
        ],
        "6": [
            # Bug: double free
            
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/ThreadSpecificEpochBasedReclamationInformation.hpp:80",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowex.hpp:121",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowexNodeBase.hpp:37",
            "targets/recipe_phot/include/hot/rowex/include/hot/rowex/HOTRowexNodeBase.hpp:48",
        ],
    },
    "p_masstree": {
        "0": [
            # Bug: leaf insert
            # non-atomic insertion of data
            
            "targets/recipe_pmasstree/src/masstree.h:1362",
            "targets/recipe_pmasstree/src/masstree.h:1368",
            "targets/recipe_pmasstree/src/masstree.h:1394",
            "targets/recipe_pmasstree/src/masstree.h:1397",
            "targets/recipe_pmasstree/src/masstree.h:1408",
            "targets/recipe_pmasstree/src/masstree.h:1410",
            "targets/recipe_pmasstree/src/masstree.h:1432",
            "targets/recipe_pmasstree/src/masstree.h:1454",
            "targets/recipe_pmasstree/src/masstree.h:1477",
            "targets/recipe_pmasstree/src/masstree.h:889",
        ],
        "1": [
            # Bug: non-atomic / non-recovered init
            
            "targets/recipe_pmasstree/src/masstree.h:2369",
        ],
        "2": [
            # Bug: non-atomic / non-recovered init
            
            "targets/recipe_pmasstree/src/masstree.h:2376",
            "targets/recipe_pmasstree/src/masstree.h:2377",
            "targets/recipe_pmasstree/src/masstree.h:2379",
            "targets/recipe_pmasstree/src/masstree.h:2009",
            "targets/recipe_pmasstree/src/masstree.h:657",
            "targets/recipe_pmasstree/src/masstree.h:661",
            "targets/recipe_pmasstree/src/masstree.h:663",
            "targets/recipe_pmasstree/src/masstree.h:664",
            "targets/recipe_pmasstree/src/masstree.h:665",
            "targets/recipe_pmasstree/src/masstree.h:668",
            "obj.c:2916",
            "obj.c:2253",
            "targets/recipe_pmasstree/src/pmdk.cpp:65",
            "targets/recipe_pmasstree/main/main.cpp:27",
        ]
    },
    # server-class
    "memcached": {
        "0": [
            # Item marked as linked with old data
            
            "items.c:519"
        ],
        "1": [
            # Ordering bug leaves data out of sync
            
            "items.c:521"
        ],
        "2": [
            # Unpersisted CAS id incurs data lose
            # Bug: do_item_link
            
            "items.c:538",
            "items.c:539",
            "items.c:540",
            "items.c:541",
            "items.c:591",
            "items.c:204",
            "thread.c:538",
            "memcached.c:4111",
            "memcached.c:5594",
            "items.c:1023",
            "items.c:550",
            "thread.c:551",
            "items.c:470",
            "items.c:1096",
            "items.c:1098",
            "items.c:1300",
            "items.c:1301",
            "items.c:1302",
            "items.c:1176",
            "items.c:1395",
            "thread.c:589",
            "thread.c:609",
            "items.c:1282",
            "items.c:1224",
            "items.c:551",
            "items.c:592",
            "items.c:469",
            "items.c:351"
        ]
    },
    "redis": {
        # non-atomic initialization
        "0": [
            "server.c:4013",
            "server.c:4027",
            "obj.c:2916",
            "server.c:503",
        ]
    },
    "hse_v1": {
        # Bug: incorrect handle of failure recovery of a partially updated log header
        "0": [
            "mdc.c:403",
            "mdc_file.c:412",
            "omf.h:72",
            "mblock_file.c:571",
            "mblock_fset.c:157",
            "mblock_fset.c:208",
            "kvdb_interface.c:252",
            "kvdb_interface.c:388",
        ]
    },
    # # Bug: incorrect handle of failure recovery of a partially updated log header
    "hse_v2": {
        "0": [
            "mdc.c:403",
            "mdc.c:198",
            "omf.h:71",
            "mblock_file.c:571",
            "mblock_fset.c:157",
            "mblock_fset.c:208",
            "kvdb_interface.c:252",
            "kvdb_interface.c:388",
        ]
    }
}

def get_bugs():
    from copy import deepcopy
    return deepcopy(BUG_LOCATIONS)