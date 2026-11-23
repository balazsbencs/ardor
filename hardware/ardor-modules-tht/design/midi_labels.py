"""Builder-facing values/types for the dense MIDI carrier."""
from expression_labels import component_label as passive_label


def component_label(ref, part):
    if ref.startswith('FB'):
        return '600R'
    if ref=='U201':
        return 'H11L1'
    if ref=='D202':
        return '1.5KE\n6.8CA'
    if ref in ['D203','D204']:
        return 'SMDJ\n24CA'
    return passive_label(ref,part)
