"""Compact assembly labels; electrical references and BOM values stay intact."""

SHARED_LABELS = {'D303': ['D304'], 'D305': ['D306']}
SHARED_POSITIONS = {'D303': (38.1, 31.25), 'D305': (48.8, 31.25)}


def component_label(ref, part):
    if ref.startswith('J'):
        return ref  # Connector IDs link to the complete back-side pin maps.
    if ref.startswith('R'):
        return part['value'].split(' / ')[0].replace('kR', 'k').replace('MR', 'M')
    if ref.startswith('C'):
        return part['value'].split(' / ')[0]
    if ref.startswith('FB'):
        return '800R\n100MHz'
    if ref in ['D301', 'D302']:
        return '1.5KE\n6.8CA'
    return part['value']
