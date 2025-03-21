from pathlib import Path
from functools import partial

from eval_targets import TARGET_GROUPS

class GraphLineBuilder:
    def __init__(self):
        self.config = {}

    def label(self, label):
        assert 'label' not in self.config
        self.config['label'] = label
        return self

    def color(self, color):
        assert 'color' not in self.config
        self.config['color'] = color
        return self

    def data(self, data: dict):
        assert 'data' not in self.config
        self.config['data'] = data
        return self

    def build(self):
        assert 'label' in self.config
        assert 'color' in self.config
        assert 'data' in self.config and len(self.config['data']) > 0
        return self.config

def create_line(label: str, color: str, data: dict) -> dict:
    return GraphLineBuilder().label(label).color(color).data(data).build()

SQUINT_REP_LABEL = 'RepTest'
SQUINT_REP_COLOR = 'tab:blue'

SQUINT_SELECTIVE_LABEL = 'ALICE'
SQUINT_SELECTIVE_COLOR = 'black'

PMDK_GROUP = 'pmdk'
KVI_GROUP = 'kvi'
RECIPE_GROUP = 'recipe'
SERVER_GROUP = 'server'
SERVER_GROUP_POSIX = 'server_posix'

GROUP_NAMES = [PMDK_GROUP, KVI_GROUP, RECIPE_GROUP, SERVER_GROUP, SERVER_GROUP_POSIX]

SQUINT_REP_LINE_FN = partial(create_line, SQUINT_REP_LABEL, SQUINT_REP_COLOR)
SQUINT_SELECTIVE_LINE_FN = partial(create_line, SQUINT_SELECTIVE_LABEL, SQUINT_SELECTIVE_COLOR)

LINE_FNS = {
    SQUINT_REP_LABEL: SQUINT_REP_LINE_FN,
    SQUINT_SELECTIVE_LABEL: SQUINT_SELECTIVE_LINE_FN,
}


