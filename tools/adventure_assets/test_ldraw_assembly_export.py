#!/usr/bin/env python3
"""Contract refusal regressions using the actual exported 39-part wall."""
import copy,json,unittest
from pathlib import Path
from validate_ldraw_assembly import validate_data
ROOT=Path(__file__).resolve().parents[2]/'data/adventure/ldraw-blacksmith-parts-r01'
class ExportContract(unittest.TestCase):
 def setUp(self):self.a=json.loads((ROOT/'assembly.json').read_text());self.w=json.loads((ROOT/'wall.json').read_text())
 def reject(self):
  with self.assertRaises(ValueError):validate_data(self.a,self.w)
 def test_actual_export(self):self.assertEqual(validate_data(self.a,self.w)['eligibleParts'],29)
 def test_unknown_schema(self):self.w['schema']=2;self.reject()
 def test_duplicate_source(self):self.a['parts'].append(self.a['parts'][0]);self.reject()
 def test_numeric_id(self):self.a['parts'][0]['sourceId']=123;self.reject()
 def test_nan_transform(self):self.a['parts'][0]['matrixRows'][0][0]=float('nan');self.reject()
 def test_modified_source_selection(self):self.w['parts'][0]['translation'][0]+=.01;self.reject()
 def test_bad_slot(self):self.w['bonds'][0]['lowerSlot']=10000;self.reject()
 def test_wrong_endpoint(self):self.w['bonds'][0]['upperPart']='1';self.reject()
 def test_duplicate_bond(self):self.w['bonds'].append(copy.deepcopy(self.w['bonds'][0]));self.reject()
 def test_invented_anchor(self):self.w['anchoredParts'].append(next(p['sourceId'] for p in self.w['parts'] if p['sourceId'] not in self.w['anchoredParts']));self.reject()
 def test_uncertain_support_release(self):next(c for c in self.w['components'] if not c['manualReleaseEligible'])['manualReleaseEligible']=True;self.reject()
 def test_fabricated_component(self):
  a,b=self.w['components'][:2];a['parts'].append(b['parts'].pop());self.reject()
 def test_capacity_raise(self):self.a['budgets']['selectedParts']=10000;self.reject()
if __name__=='__main__':unittest.main()
