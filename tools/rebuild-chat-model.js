'use strict';
// Development-only model builder. OBS inference remains native C++.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os'),zlib=require('node:zlib');
const root=path.resolve(__dirname,'..');
const value=flag=>{const i=process.argv.indexOf(flag);return i<0?null:process.argv[i+1];};
const upstream=value('--guesser-root');
if(!upstream)throw new Error('Usage: node tools/rebuild-chat-model.js --guesser-root PATH [--output-dir PATH]');
const output=path.resolve(value('--output-dir')||path.join(root,'data/language-guesser'));
const {buildModel}=require(path.join(path.resolve(upstream),'training/build-model'));
const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'bh-obs-training-'));
try{
 const extraPath=path.join(root,'training/chat-expansion.jsonl');
 const rows=fs.readFileSync(extraPath,'utf8').trim().split(/\r?\n/u).map(s=>JSON.parse(s));
 const languages=[...new Set(rows.map(row=>row.language))];
 const baseline=JSON.parse(zlib.gunzipSync(fs.readFileSync(path.join(root,'training/baseline-model.json.gz'))));
 const built=buildModel({sources:[path.join(root,'training/base-corpus.jsonl'),extraPath],includeSeed:false,maxFeatures:5000,output:path.join(temporary,'candidate.json')}).model;
 const result=JSON.parse(JSON.stringify(baseline)),alpha=0.5;
 for(const code of languages){
  const current=built.languages[code].vector,prior=baseline.languages[code].vector;
  const adjusted=Object.fromEntries(Object.entries(current).map(([gram,w])=>[gram,w*(baseline.idf[gram]||1)/(built.idf[gram]||1)]));
  let magnitude=Math.sqrt(Object.values(adjusted).reduce((sum,w)=>sum+w*w,0));
  for(const gram of Object.keys(adjusted))adjusted[gram]/=magnitude;
  const combined=Object.fromEntries([...new Set([...Object.keys(adjusted),...Object.keys(prior)])].map(gram=>[gram,(1-alpha)*(prior[gram]||0)+alpha*(adjusted[gram]||0)]).sort((a,b)=>b[1]-a[1]||(a[0]<b[0]?-1:a[0]>b[0]?1:0)).slice(0,5000));
  magnitude=Math.sqrt(Object.values(combined).reduce((sum,w)=>sum+w*w,0));
  for(const gram of Object.keys(combined))combined[gram]/=magnitude;
  result.languages[code]={samples:built.languages[code].samples,vector:combined};
 }
 result.createdAt='2026-11-04T00:00:00.000Z';
 result.training={...built.training,updateMethod:'selective-profile-blend',profileBlend:alpha,sourceSamples:rows.length,expandedLanguages:languages,revision:'obs-chat-20261104-1'};
 fs.mkdirSync(output,{recursive:true});
 fs.writeFileSync(path.join(output,'model.json'),JSON.stringify(result)+'\n');
 console.log(JSON.stringify({samples:result.training.totalSamples,additionalSamples:rows.length,updatedLanguages:languages.length,preservedLanguages:Object.keys(baseline.languages).length-languages.length,featuresCappedAt:5000}));
}finally{fs.rmSync(temporary,{recursive:true,force:true});}
