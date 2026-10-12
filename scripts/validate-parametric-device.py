#!/usr/bin/env python3
"""Exercise the real manager on an isolated device data root (no mocked API).

Uploads test assets and writes bank 98. Use a temporary manager/data root only.
Run --runtime while the matching audio process is running on that data root.
"""
import argparse
import json
from pathlib import Path
import time
import urllib.error
import urllib.parse
import urllib.request


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-url', default='http://127.0.0.1:4189')
    parser.add_argument('--model-dir', type=Path, required=True)
    parser.add_argument('--runtime', action='store_true')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    results = []

    def request(method, path, body=None, content_type='application/json', expected=200):
        if body is not None and not isinstance(body, bytes):
            body = json.dumps(body).encode()
        start = time.monotonic()
        req = urllib.request.Request(args.base_url + path, data=body, method=method,
                                     headers={'Content-Type': content_type})
        try:
            with urllib.request.urlopen(req, timeout=60) as response:
                status, payload = response.status, response.read()
        except urllib.error.HTTPError as error:
            status, payload = error.code, error.read()
        result = json.loads(payload) if payload else None
        allowed = expected if isinstance(expected, tuple) else (expected,)
        assert status in allowed, (method, path, status, result)
        results.append(dict(method=method, path=path, status=status,
                            milliseconds=1000*(time.monotonic()-start)))
        return result

    def upload(filename, payload, expected=201):
        boundary = 'ardor-hardware-validation-boundary'
        body = (f'--{boundary}\r\nContent-Disposition: form-data; name="file"; filename="{filename}"\r\n'
                'Content-Type: application/octet-stream\r\n\r\n').encode()
        body += payload + f'\r\n--{boundary}--\r\n'.encode()
        return request('POST', '/api/assets/models', body,
                       'multipart/form-data; boundary='+boundary, expected)

    def apply(slot, expected_state='applied'):
        start = time.monotonic()
        queued = request('POST', f'/api/presets/banks/98/slots/{slot}/apply', expected=202)
        for _ in range(600):
            status = request('GET', '/api/runtime/apply/'+queued['id'], expected=(200, 202))
            if status['state'] != 'pending':
                assert status['state'] == expected_state, status
                results.append(dict(apply_slot=slot, milliseconds=1000*(time.monotonic()-start),
                                    state=status['state']))
                return
            time.sleep(.1)
        raise AssertionError('apply timeout')

    sources = ['clone-overdrive.nam', 'Ardor-Parametric-Overdrive-Demo.nam',
               'a2-dual-0.nam', 'film-dual-0.nam']
    imported = []
    for source in sources:
        payload = (args.model_dir/source).read_bytes()
        filename = 'Hardware '+source
        info = upload(filename, payload)
        assert info['nam']['supported'], info
        assert len(info['nam']['parameters']) == 2, info
        upload(filename, payload, expected=409)
        imported.append(info)
        results.append(dict(asset=info['filename'], metadata=info['nam']))

    unknown = json.loads((args.model_dir/'Ardor-Parametric-Overdrive-Demo.nam').read_text())
    unknown['architecture'] = 'HyperWaveNet'
    info = upload('Hardware unsupported.nam', json.dumps(unknown).encode())
    assert not info['nam']['supported'] and info['nam']['error'], info
    # The asset store retains unplayable assets with an explicit error, rather
    # than inventing playable controls. Inference validates the complete weights.
    broken = upload('Hardware malformed.nam', b'{invalid json')
    assert not broken['nam']['supported'] and broken['nam']['error'], broken
    listed = request('GET', '/api/assets/models')['assets']
    assert all(any(asset['id']==item['id'] for asset in listed) for item in imported)
    presets = []
    for slot, info in enumerate(imported):
        params = {p['name']:p['max'] for p in info['nam']['parameters']}
        preset = dict(version=1, name='Hardware '+str(slot), routing='serial',
                      blocks=[dict(id='pedal', type='namPedal', enabled=True,
                                   asset=info['path'], params=dict(useNano=True,modelParameters=params))])
        preset['global'] = dict(inputGainDb=0,outputGainDb=-30,safetyLimitDb=-1)
        request('PUT', f'/api/presets/banks/98/slots/{slot}', preset)
        saved = request('GET', f'/api/presets/banks/98/slots/{slot}')['preset']
        assert saved['blocks'][0]['params']==preset['blocks'][0]['params'], saved
        presets.append(preset)

    original = imported[0]
    renamed = request('PATCH', '/api/assets/models/'+urllib.parse.quote(original['id'],safe=''),
                      dict(filename='Hardware renamed clone.nam'))
    assert renamed['asset']['nam']==original['nam'], renamed
    assert renamed['updatedPresetCount']==1, renamed
    presets[0] = request('GET','/api/presets/banks/98/slots/0')['preset']
    assert presets[0]['blocks'][0]['asset']==renamed['asset']['path']
    if args.runtime:
        # Ten cycles × four architectures, changing controls and size every cycle.
        for cycle in range(10):
            for slot, preset in enumerate(presets):
                specs = imported[slot]['nam']['parameters']
                preset['blocks'][0]['params'] = dict(useNano=cycle%2==0,
                    modelParameters={p['name']:p['min'] if cycle%2==0 else p['max'] for p in specs})
                request('PUT',f'/api/presets/banks/98/slots/{slot}',preset)
                apply(slot)
        # Keep the final test preset in Nano before the next live measurement.
        presets[0]['blocks'][0]['params']['useNano'] = True
        request('PUT','/api/presets/banks/98/slots/0',presets[0])
        apply(0)
        invalid = json.loads(json.dumps(presets[3]))
        invalid['blocks'][0]['asset'] = broken['path']
        invalid['blocks'][0]['params'] = {}
        request('PUT','/api/presets/banks/98/slots/3',invalid)
        active_before = request('GET', '/api/device')['active']
        apply(3, expected_state='rejected')
        # An invalid model must not replace the active prepared engine.
        active_after = request('GET', '/api/device')['active']
        assert active_before == active_after, (active_before, active_after)
        results.append(dict(invalid_model_preserved_active=True, active=active_after))
        apply(0)
    args.output.write_text(json.dumps(dict(passed=True,runtime=args.runtime,results=results),indent=2)+'\n')
    print(f'PASS: {len(results)} observations; runtime={args.runtime}')


if __name__ == '__main__':
    main()
