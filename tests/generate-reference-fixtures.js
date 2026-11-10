'use strict';
const fs=require('fs');
const path=require('path');
const upstream=path.resolve(process.argv[2]||'');
if(!process.argv[2]) throw new Error('Usage: node generate-reference-fixtures.js PATH_TO_LANGUAGE_GUESSER_0.11.5 [MODEL_PATH]');
const {LanguageDetector}=require(path.join(upstream,'src/detector/LanguageDetector'));
const {MLClassifier}=require(path.join(upstream,'src/detector/MLClassifier'));
const originalVectorize=MLClassifier.prototype.vectorize;
MLClassifier.prototype.vectorize=function(text){
 const featureText=text.replace(/’/gu,"'").replace(/(^|\s)'+(?=[\p{L}\p{M}\p{N}])/gu,'$1').replace(/(?<=[\p{L}\p{M}\p{N}])'+(?=\s|$)/gu,'');
 return originalVectorize.call(this,featureText);
};
const d=new LanguageDetector({modelPath:process.argv[3] ? path.resolve(process.argv[3]) : path.join(upstream,'src/model/model.json'),preferredLanguage:'en',confidenceThreshold:.72,mixedLanguageAnalysis:false});
const before=JSON.parse(fs.readFileSync(path.join(__dirname,'native-fixtures.json')));
const rows=before.map(({job})=>{
 const r=d.detect(job.text,{preferredLanguage:job.target||'en',knownEmotes:job.knownEmotes||[],mixedLanguageAnalysis:false});
 const words=(r.normalized.match(/\p{Script=Han}|[\p{Script=Hiragana}\p{Script=Katakana}]|\p{L}[\p{L}\p{M}]*/gu)||[]).length;
 const useful=(r.normalized.match(/[\p{L}\p{M}]/gu)||[]).length;
 const confidence=useful<4?0:r.automationConfidence;
 return {job,expected:{normalized:r.normalized,language:useful<4?'unknown':r.language||r.candidates[0]?.language,confidence,automationReliable:useful<4?false:r.automationReliable,translate:words>=4&&r.needsTranslation&&r.automationReliable&&confidence>=.85}};
});
fs.writeFileSync(path.join(__dirname,'native-fixtures.json'),JSON.stringify(rows,null,2)+'\n');
