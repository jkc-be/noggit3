import copy
import math
import unittest
from export import transform, render, ZEROPOINT
from catalog import split_row, tuples

P='6d42fe45-bb86-4929-a6ef-0102dfe9daf2'
O='6d42fe45-bb86-4929-a6ef-0102dfe9daf3'
class ExportTests(unittest.TestCase):
    def root(self):
        return dict(version=1,project=P,map=0,objects=[dict(key=O,entry=42,display=10,phase=1,
                    position=[ZEROPOINT+100,60,ZEROPOINT+8900],rotation=[0,90,0],scale=1)])
    def test_coordinate_basis(self):
        # Northshire-like coordinates; editor yaw 90 means local X points along server +X.
        self.assertEqual(transform(self.root()['objects'][0]['position'],[0,90,0]),(-8900.,-100.,60.,0.,0.,1.))
        for yaw in (-180,-90,0,45,90,179):
            o=transform([ZEROPOINT,0,ZEROPOINT],[0,yaw,0])[3]
            # Independently transform local +X through the editor Y rotation and world axis conversion.
            a=math.radians(yaw)
            self.assertAlmostEqual(math.cos(o),math.sin(a))
            self.assertAlmostEqual(math.sin(o),-math.cos(a))
    def test_reject_unsupported(self):
        for change in ({'rotation':[1,0,0]},{'scale':2},{'phase':0},{'position':[math.nan,0,0]}, {'key':"x';DROP TABLE gameobject;"}):
            root=self.root(); root['objects'][0].update(change)
            with self.assertRaises(ValueError): render(root)
    def test_ownership_and_atomicity(self):
        sql=render(self.root())
        self.assertIn('START TRANSACTION;',sql)
        self.assertIn('ROLLBACK;',sql)
        self.assertIn('RESIGNAL;',sql)
        self.assertIn('g.Comment <=>',sql)
        self.assertIn('t.displayId<>s.display',sql)
        self.assertIn("WHERE l.project='"+P+"'",sql)
        self.assertNotIn('REPLACE INTO',sql)
        self.assertNotIn('MAX(guid)',sql)
    def test_duplicate_identity(self):
        root=self.root(); root['objects']*=2
        with self.assertRaises(ValueError): render(root)
    def test_empty_project_is_scoped_removal(self):
        root=self.root(); root['objects']=[]
        sql=render(root)
        self.assertNotIn('INSERT INTO `noggit_stage` VALUES',sql)
        self.assertIn('s.object_key IS NULL',sql)
        self.assertIn(P,sql)
    def test_mysqldump_insert_formats(self):
        text = "INSERT INTO `gameobject_template` VALUES (1,5,2,'x;,(y)','','','',1),(2,5,3,'z','','','',1);\nINSERT INTO `gameobject_template` VALUES (3,5,4,'a','','','',1);"
        self.assertEqual([split_row(t)[0] for t in tuples(text)],['1','2','3'])
    def test_dump_escaping(self):
        self.assertEqual(split_row("(1,5,2,'Bob\\'s, barrel','','','',1);"),['1','5','2',"Bob's, barrel",'','','','1'])

if __name__=='__main__': unittest.main()
