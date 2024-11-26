namespace LibProjectNet8;

public class Class1
{

    public void TestMethodNet8()
    {
        Console.WriteLine("This is method of net 8 calling a method in net 6 \n");
        var lib6 = new LibProjectNet6.Class1();
        lib6.TestMethodNet6();

    }

}
