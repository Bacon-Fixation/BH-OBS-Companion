#!/usr/bin/env python3
"""Compare native models on disjoint, labeled chat sentences and perturbations."""
import argparse,collections,hashlib,json,os,pathlib,statistics,subprocess,unicodedata
ROOT=pathlib.Path(__file__).resolve().parents[1]
def normalized(text):return ''.join(c for c in unicodedata.normalize('NFKC',text).casefold() if c.isalnum())
def read_rows(path):return [json.loads(s) for s in pathlib.Path(path).read_text(encoding='utf8').splitlines() if s.strip()]
def run(runner,data,rows,variant):
 jobs=[];truth=[]
 for row in rows:
  text=row['text']
  if variant=='quoted':text=' '.join('‘'+w+'’' for w in text.split())
  elif variant=='chat-noise':text='@chatuser '+text+' Kappa https://example.invalid/clip'
  for target in ['en',row['language'] if row['language']!='en' else 'es']:
   jobs.append({'id':str(len(jobs)),'text':text,'target':target,'knownEmotes':['Kappa']});truth.append(row)
 result=subprocess.run([str(runner),str(data)],input=''.join(json.dumps(j,ensure_ascii=False)+'\n' for j in jobs),text=True,encoding='utf8',capture_output=True,check=True)
 actual=[json.loads(line) for line in result.stdout.splitlines()]
 if len(actual)!=len(jobs) or any('error' in a for a in actual):raise RuntimeError('Native runner failed or omitted a result')
 labels=sum(a['language']==e['language'] for a,e in zip(actual,truth))
 tp=sum(a.get('translate',False) and j['target']!=e['language'] for a,j,e in zip(actual,jobs,truth))
 fp=sum(a.get('translate',False) and j['target']==e['language'] for a,j,e in zip(actual,jobs,truth))
 scores=[a.get('languageConfidence',a.get('rawTopConfidence',0)) for a,e in zip(actual,truth) if a['language']==e['language']]
 languages=collections.defaultdict(lambda:{'cases':0,'correct':0,'falseTranslations':0,'foreignTranslations':0})
 for a,j,e in zip(actual,jobs,truth):
  group=languages[e['language']];group['cases']+=1;group['correct']+=a['language']==e['language'];group['falseTranslations']+=bool(a.get('translate') and j['target']==e['language']);group['foreignTranslations']+=bool(a.get('translate') and j['target']!=e['language'])
 return {'sentences':len(rows),'cases':len(jobs),'correctLabels':labels,'labelAccuracy':labels/len(jobs),'foreignTranslations':tp,'eligibleForeignCases':len(rows),'translationRecall':tp/len(rows),'falseTranslations':fp,'translationPrecision':tp/(tp+fp) if tp+fp else None,'highConfidenceWrong':sum(a['language']!=e['language'] and a.get('languageConfidence',0)>=.85 for a,e in zip(actual,truth)),'meanCorrectLanguageScore':statistics.mean(scores),'byLanguage':dict(sorted(languages.items()))}
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--runner',required=True);p.add_argument('--before-data',required=True);p.add_argument('--after-data',required=True);p.add_argument('--output',required=True);p.add_argument('--corpus',default=str(ROOT/'tests/chat-heldout.jsonl'));p.add_argument('--allow-regressions',action='store_true');a=p.parse_args()
 rows=read_rows(a.corpus);keys=[normalized(row['text']) for row in rows]
 if len(keys)!=len(set(keys)):raise ValueError('Evaluation contains duplicate normalized text')
 train={normalized(row['text']) for f in (ROOT/'training').glob('*corpus.jsonl') for row in read_rows(f)}|{normalized(row['text']) for row in read_rows(ROOT/'training/chat-expansion.jsonl')}
 if set(keys)&train:raise ValueError('Training/evaluation overlap detected')
 report={'corpus':pathlib.Path(a.corpus).name,'corpusSha256':hashlib.sha256(pathlib.Path(a.corpus).read_bytes()).hexdigest(),'uniqueSentences':len(rows),'languages':len(set(row['language'] for row in rows)),'trainingOverlap':0,'scoreNote':'Candidate scores are model evidence, not calibrated correctness probabilities.','acceptanceCriteria':'No exact-label regression, no increase in false target-language translations or high-score label errors. Coverage is reported separately.','variants':{},'regressions':[],'coverageChanges':{}}
 for variant in ['plain','quoted','chat-noise']:
  before=run(a.runner,a.before_data,rows,variant);after=run(a.runner,a.after_data,rows,variant);report['variants'][variant]={'before':before,'after':after}
  report['coverageChanges'][variant]=after['foreignTranslations']-before['foreignTranslations']
  for metric in ['correctLabels']:
   if after[metric]<before[metric]:report['regressions'].append(f'{variant}: {metric} regressed')
  for metric in ['falseTranslations','highConfidenceWrong']:
   if after[metric]>before[metric]:report['regressions'].append(f'{variant}: {metric} increased')
 pathlib.Path(a.output).write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
 print(json.dumps({**{k:v for k,v in report.items() if k!='variants'},'variants':{k:{phase:{m:n for m,n in metrics.items() if m!='byLanguage'} for phase,metrics in v.items()} for k,v in report['variants'].items()}},indent=2))
 if report['regressions'] and not a.allow_regressions:raise SystemExit(1)
if __name__=='__main__':main()
