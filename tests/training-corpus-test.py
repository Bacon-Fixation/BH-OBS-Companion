#!/usr/bin/env python3
"""Audit training provenance, leakage and the scope of the bundled profile update."""
import collections,gzip,json,math,pathlib,unittest,unicodedata
ROOT=pathlib.Path(__file__).resolve().parents[1]
def read(name):return [json.loads(line) for line in (ROOT/name).read_text(encoding='utf8').splitlines() if line.strip()]
def key(text):return ''.join(c for c in unicodedata.normalize('NFKC',text).casefold() if c.isalnum())
class TrainingAudit(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.base=read('training/base-corpus.jsonl');cls.extra=read('training/chat-expansion.jsonl');cls.model=json.loads((ROOT/'data/language-guesser/model.json').read_text(encoding='utf8'));cls.old=json.loads(gzip.decompress((ROOT/'training/baseline-model.json.gz').read_bytes()))
 def test_new_labeled_examples_and_counts(self):
  self.assertEqual(len(self.extra),428);self.assertEqual(len({r['language'] for r in self.extra}),24)
  prior={(r['language'],key(r['text'])) for r in self.base};new={(r['language'],key(r['text'])) for r in self.extra}
  self.assertEqual(len(new),len(self.extra));self.assertFalse(prior&new)
  self.assertTrue(all(r['source']=='authored-synthetic-obs-chat-2026-11-04' and len(r['text'])<=2000 for r in self.extra))
  self.assertEqual(self.model['training']['totalSamples'],len(self.base)+len(self.extra))
  self.assertEqual(sum(p['samples'] for p in self.model['languages'].values()),self.model['training']['totalSamples'])
 def test_holdouts_are_disjoint(self):
  train={key(r['text']) for r in self.base+self.extra};seen=set()
  for file in ['tests/chat-development.jsonl','tests/chat-heldout.jsonl']:
   rows=read(file);keys={key(r['text']) for r in rows}
   self.assertEqual(len(keys),len(rows));self.assertFalse(keys&train);self.assertFalse(keys&seen);seen|=keys
 def test_unexpanded_profiles_and_idf_are_preserved(self):
  updated={r['language'] for r in self.extra};self.assertEqual(self.model['idf'],self.old['idf']);self.assertEqual(len(self.model['languages']),214)
  for code,profile in self.model['languages'].items():
   with self.subTest(language=code):
    if code not in updated:self.assertEqual(profile,self.old['languages'][code])
    else:self.assertNotEqual(profile['vector'],self.old['languages'][code]['vector'])
    self.assertLessEqual(len(profile['vector']),5000)
    self.assertAlmostEqual(sum(w*w for w in profile['vector'].values()),1,places=8)
 def test_recorded_evaluation_safety_and_accuracy(self):
  for file in ['tests/chat-development-comparison.json','tests/chat-heldout-comparison.json']:
   report=json.loads((ROOT/file).read_text(encoding='utf8'));self.assertEqual(report['trainingOverlap'],0)
   for variant,groups in report['variants'].items():
    with self.subTest(corpus=file,variant=variant):
     before,after=groups['before'],groups['after'];self.assertGreaterEqual(after['correctLabels'],before['correctLabels']);self.assertLessEqual(after['falseTranslations'],before['falseTranslations']);self.assertLessEqual(after['highConfidenceWrong'],before['highConfidenceWrong'])
if __name__=='__main__':unittest.main()
