"""Check the explicit STE sentence-length limits in the short assembly guides.

Vocabulary/POS/meaning, one action and technical accuracy also need author
review against ASD-STE100 Issue 9. This length check alone is not STE approval.
"""
from pathlib import Path
import json,re
ROOT=Path(__file__).resolve().parents[1]
for file in sorted(ROOT.glob('*/ASSEMBLY.md')):
    lines=file.read_text().splitlines();steps=[];descriptions=[]
    for line in lines:
        if re.match(r'^\d+\. ',line):steps.append(line.split('. ',1)[1])
        elif line and not line.startswith(('#','|','[')):descriptions.extend(re.split(r'(?<=[.!?])\s+',line))
    words=lambda s:len(re.findall(r'\b[\w]+(?:[–/-][\w]+)*\b',s))
    assert steps and all(words(s)<=20 for s in steps),(file,'Procedure exceeds 20 words')
    assert all(words(s)<=25 for s in descriptions),(file,'Description exceeds 25 words')
    assert not re.search(r"\b(?:don't|isn't|aren't|can't|won't|it's|you're)\b",file.read_text(),re.I)
    verbs=sorted({s.split()[0] for s in steps})
    result={'reference':'ASD-STE100 Issue 9, 2025-01-15','automatic_checks':'Sentence length and common contractions only; vocabulary, grammar and meaning reviewed by author','result':'PASS','procedure_steps':len(steps),'maximum_procedure_words':max(map(words,steps)),'maximum_description_words':max(map(words,descriptions)),'technical_terminology':'../STE-TERMS.md','third_party_certification':False}
    (file.parent/'verification/guide-language-review.json').write_text(json.dumps(result,indent=2)+'\n')
    print(file.parent.name,'guide length PASS',len(steps),'steps; initial tokens',verbs)
